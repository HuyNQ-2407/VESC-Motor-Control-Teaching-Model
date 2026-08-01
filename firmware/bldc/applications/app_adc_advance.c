#include "app.h"
#include "ch.h"
#include "hal.h"

// VESC
#include "mc_interface.h"
#include "utils_math.h"
#include "timeout.h"
#include "commands.h"
#include "terminal.h"
#include "buffer.h"
#include "hw.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

// Configuration
#define VOLT_CENTER         1.65f
#define VOLT_MIN            0.4f
#define VOLT_MAX            3.1f
#define DEADZONE            0.1f
#define UPDATE_RATE_HZ      100
#define FILTER_SAMPLES      5
#define PID_KP				0.001f
#define PID_KI				0.0001f
#define PID_KD				0.00001f
#define PID_DT				0.01f
#define PID_TAU				0.02f
#define PID_MAX_INTEGRATOR	0.5f
#define PID_MAX_OUTPUT		1.5f
#define STATUS_RATE_HZ		0.33f

// ChibiOS compatibility shim (for 3.0.5)
#ifndef TIME_MS2I
  #define TIME_MS2I(ms) MS2ST(ms)
#endif

#ifndef TIME_S2I
  #define TIME_S2I(s)   S2ST(s)
#endif

#ifndef TIME_US2I
  #define TIME_US2I(us) US2ST(us)
#endif

// Threads
static THD_FUNCTION(advance_adc_thread, arg);
static THD_WORKING_AREA(advance_adc_thread_wa, 2048);
static THD_FUNCTION(uart_thread, arg);
static THD_WORKING_AREA(uart_thread_wa, 2048);
static THD_FUNCTION(uart_rx_thread, arg);
static THD_WORKING_AREA(uart_rx_wa, 1024);

typedef struct {
	float kp, ki, kd;
	float integrator;
	float max_integrator;
	float prev_error;
	float prev_feedback;
	float max_output, min_output;
	float T;
	float tau;
	float differentiator;
} PID_controller_t;

static float read_adc(void);
static float pid_calculate(PID_controller_t *pid, float setpoint, float feedback);
static void terminal_adc_status(int argc, const char **argv);
static void terminal_set_kp(int argc, const char **argv);
static void terminal_set_ki(int argc, const char **argv);
static void terminal_set_kd(int argc, const char **argv);
static void terminal_reset_pid(int argc, const char **argv);
static void terminal_remote_control(int argc, const char **argv);
static void terminal_set_rpm(int argc, const char **argv);
static void terminal_motor_info(int argc, const char **argv);
static void terminal_set_limit(int argc, const char **argv);
static void process_line(const char *s);
static float erpm_to_rpm(float erpm);
static void uart_printf(const char *fmt, ...);

static PID_controller_t speed_pid = {
	.kp = PID_KP,
	.ki = PID_KI,
	.kd = PID_KD,
	.integrator = 0.0f,
	.max_integrator = PID_MAX_INTEGRATOR,
	.max_output = PID_MAX_OUTPUT,
	.min_output = -PID_MAX_OUTPUT,
	.T = PID_DT,
	.prev_error = 0.0f,
	.prev_feedback = 0.0f,
	.tau = PID_TAU
};

static volatile bool stop_now = true;
static volatile bool is_running = false;
static volatile bool remote_control_mode = false;
static volatile float adc_voltage = 0.0;
static volatile float adc_normalized = 0.0;
static volatile float set_current = 0.0;
static volatile float pid_error;
static volatile float runtime_max_erpm = 0.0f;
static volatile float runtime_max_current = 0.0f;
static float u_voltage = 0.0f;
static float u_current = 0.0f;
static float u_duty    = 0.0f;
static float u_rpm_ref = 0.0f;
static float u_rpm_vesc = 0.0f;
volatile float set_erpm_custom = 0.0;
static volatile float remote_set_erpm = 0.0f;

void app_custom_start(void) {
#ifdef HW_ADC_EXT_GPIO
	palSetPadMode(HW_ADC_EXT_GPIO, HW_ADC_EXT_PIN, PAL_MODE_INPUT_ANALOG);
#endif
	stop_now = false;

	// UART setup
	static SerialConfig uart_cfg = {
		115200,
		0,
		0,
		0
	};
	sdStart(&HW_UART_DEV, &uart_cfg);
	palSetPadMode(HW_UART_TX_PORT, HW_UART_TX_PIN, PAL_MODE_ALTERNATE(HW_UART_GPIO_AF) | PAL_STM32_OSPEED_HIGHEST | PAL_STM32_PUDR_PULLUP);
	palSetPadMode(HW_UART_RX_PORT, HW_UART_RX_PIN, PAL_MODE_ALTERNATE(HW_UART_GPIO_AF) | PAL_STM32_OSPEED_HIGHEST | PAL_STM32_PUDR_PULLUP);

	terminal_register_command_callback(
			"status",
			"Show Status",
			"",
			terminal_adc_status);
	terminal_register_command_callback(
			"kp",
			"Set kp",
			"<kp>",
			terminal_set_kp);
	terminal_register_command_callback(
			"ki",
			"Set ki",
			"<ki>",
			terminal_set_ki);
	terminal_register_command_callback(
			"kd",
			"Set kd",
			"<kd>",
			terminal_set_kd);
	terminal_register_command_callback(
			"reset",
			"Reset PID",
			"",
			terminal_reset_pid);
	terminal_register_command_callback(
			"remote",
			"Enable/disable remote control",
			"[on/off]",
			terminal_remote_control);
	terminal_register_command_callback(
			"setrpm",
			"Set target speed (ERPM) from remote",
			"<erpm>",
			terminal_set_rpm);
	terminal_register_command_callback(
			"motorinfo",
			"Show motor configuration",
			"",
			terminal_motor_info);
	terminal_register_command_callback(
			"setlimit",
			"Set limits for motor in runtime",
			"[maxrpm/maxcurrent] <value>",
			terminal_set_limit);

	speed_pid.integrator = 0.0f;
	speed_pid.prev_error = 0.0f;
	speed_pid.differentiator = 0.0f;

	chThdCreateStatic(advance_adc_thread_wa, sizeof(advance_adc_thread_wa), NORMALPRIO, advance_adc_thread, NULL);
	chThdCreateStatic(uart_thread_wa, sizeof(uart_thread_wa), NORMALPRIO-1, uart_thread, NULL);
	chThdCreateStatic(uart_rx_wa, sizeof(uart_rx_wa), NORMALPRIO, uart_rx_thread, NULL);
}

// Stops the thread and releases callbacks
void app_custom_stop(void) {
	terminal_unregister_callback(terminal_adc_status);
	terminal_unregister_callback(terminal_set_kp);
	terminal_unregister_callback(terminal_set_ki);
	terminal_unregister_callback(terminal_set_kd);
	terminal_unregister_callback(terminal_reset_pid);
	terminal_unregister_callback(terminal_remote_control);
	terminal_unregister_callback(terminal_set_rpm);
	terminal_unregister_callback(terminal_motor_info);
	terminal_unregister_callback(terminal_set_limit);

	stop_now = true;
	while (is_running) {
		chThdSleepMilliseconds(1);
	}
	mc_interface_set_current(0);
}

void app_custom_configure(app_configuration *conf) {
	(void)conf;
}

static THD_FUNCTION(advance_adc_thread, arg) {
	(void)arg;

	chRegSetThreadName("APP_ADC_ADVANCE");
	is_running = true;

	systime_t sleep_time = CH_CFG_ST_FREQUENCY / UPDATE_RATE_HZ;
	if (sleep_time == 0) {
		sleep_time = 1;
	}

	for (;;) {
		chThdSleep(sleep_time);
		if (stop_now) {
			is_running = false;
			return;
		}

		// Safety: clear PID state on fault
		if (mc_interface_get_fault() != FAULT_CODE_NONE) {
			mc_interface_set_current(0);
			speed_pid.integrator = 0.0f;
			speed_pid.prev_error = 0.0f;
			speed_pid.differentiator = 0.0f;
		}

		if (app_is_output_disabled()) {
			speed_pid.integrator = 0.0f;
			speed_pid.prev_error = 0.0f;
			speed_pid.differentiator = 0.0f;
			continue;
		}

		// Choose input source: remote (host app) or local ADC
		if (remote_control_mode) {
			set_erpm_custom = remote_set_erpm;
		} else {
			float adc_signal = read_adc();
			utils_deadband(&adc_signal, DEADZONE, 1.0f);

			const volatile mc_configuration *mcconf = mc_interface_get_configuration();
			float max_erpm = mcconf->l_max_erpm;
			float min_erpm = mcconf->l_min_erpm;

			if (adc_signal >= 0.0f) {
				set_erpm_custom = adc_signal * max_erpm;
			} else {
				set_erpm_custom = adc_signal * fabsf(min_erpm);
			}
		}

		float rpm_now = mc_interface_get_rpm();
		set_current = pid_calculate(&speed_pid, set_erpm_custom, rpm_now);
		mc_interface_set_current(set_current);

		timeout_reset();
	}
}

static THD_FUNCTION(uart_thread, arg) {
	(void)arg;
	chRegSetThreadName("UART Thread");

	while (true) {
		u_voltage = GET_INPUT_VOLTAGE();
		u_current = mc_interface_read_reset_avg_input_current();
		u_duty    = mc_interface_get_duty_cycle_now();
		u_rpm_ref = (double)erpm_to_rpm((double)set_erpm_custom);
		u_rpm_vesc = (double)erpm_to_rpm(mc_interface_get_rpm());

		float temp_mosfet = mc_interface_temp_fet_filtered();
		float temp_motor = mc_interface_temp_motor_filtered();

		char msg[128];
		int len = sprintf(msg, "%.2f,%.2f,%.2f,%.1f,%.1f,%.1f,%.1f\n",
						  (double)u_voltage, (double)u_current, (double)u_duty,
						  (double)u_rpm_ref, (double)u_rpm_vesc,
						  (double)temp_mosfet, (double)temp_motor);

		// Raw text telemetry frame to the ESP32 bridge
		sdWrite(&HW_UART_DEV, (uint8_t*)msg, len);

		chThdSleepMilliseconds(50); // 20 Hz
	}
}

static THD_FUNCTION(uart_rx_thread, arg) {
	(void)arg;
	chRegSetThreadName("uart_rx");

	char line[128];
	int idx = 0;

	for (;;) {
		msg_t c = sdGetTimeout(&HW_UART_DEV, TIME_MS2I(20));
		if (c == MSG_TIMEOUT) continue;

		if (c == '\r') continue;
		if (c == '\n') {
			line[idx] = 0;
			if (idx > 0) {
				process_line(line);
				idx = 0;
			}
		} else {
			if (idx < (int)sizeof(line) - 1) {
				line[idx++] = (char)c;
			} else {
				idx = 0; // overflow, reset
			}
		}
	}
}

static float pid_calculate(PID_controller_t *pid, float setpoint, float feedback) {
	float error = setpoint - feedback;
	pid_error = error;

	float P = pid->kp * error;

	// Trapezoidal integration
	pid->integrator = pid->integrator + (pid->ki * pid->T / 2.0f) * (error + pid->prev_error);

	// Dynamic anti-windup: clamp integrator to what's left of the output range after P
	float limMaxInt, limMinInt;
	if (pid->max_output > P) {
		limMaxInt = pid->max_output - P;
	} else {
		limMaxInt = 0.0f;
	}
	if (pid->min_output < P) {
		limMinInt = pid->min_output - P;
	} else {
		limMinInt = 0.0f;
	}

	if (pid->integrator > limMaxInt) {
		pid->integrator = limMaxInt;
	} else if (pid->integrator < limMinInt) {
		pid->integrator = limMinInt;
	}

	// Low-pass filtered derivative on measurement (avoids derivative kick)
	pid->differentiator = (2.0f * pid->kd * (feedback - pid->prev_feedback)
						+ (2.0f * pid->tau - pid->T) * pid->differentiator)
						/ (2.0f * pid->tau + pid->T);

	pid->prev_error = error;
	pid->prev_feedback = feedback;

	float output = P + pid->integrator + pid->differentiator;

	const volatile mc_configuration *mcconf = mc_interface_get_configuration();
	float effective_max_current = (runtime_max_current > 0) ?
		runtime_max_current : mcconf->l_current_max;

	float max_out = fminf(pid->max_output, effective_max_current);
	float min_out = fmaxf(pid->min_output, -effective_max_current);

	if (output > max_out) {
		output = max_out;
	} else if (output < min_out) {
		output = min_out;
	}

	return output;
}

static float read_adc(void) {
	float voltage = ADC_VOLTS(ADC_IND_EXT);

	// Moving average filter
	static float filter_buffer[FILTER_SAMPLES] = {0};
	static int filter_index = 0;

	filter_buffer[filter_index] = voltage;
	filter_index = (filter_index + 1) % FILTER_SAMPLES;

	float sum = 0.0f;
	for (int i = 0; i < FILTER_SAMPLES; i++) {
		sum += filter_buffer[i];
	}
	voltage = sum / FILTER_SAMPLES;
	adc_voltage = voltage;

	// Map voltage to normalized signal (-1 to 1)
	float normalized;
	if (voltage <= VOLT_CENTER) {
		normalized = utils_map(voltage, VOLT_MIN, VOLT_CENTER, -1.0f, 0.0f);
	} else {
		normalized = utils_map(voltage, VOLT_CENTER, VOLT_MAX, 0.0f, 1.0f);
	}
	utils_truncate_number(&normalized, -1.0f, 1.0f);
	adc_normalized = normalized;

	return normalized;
}

static void terminal_adc_status(int argc, const char **argv) {
	(void)argc;
	(void)argv;

	float adc_after_deadband = adc_normalized;
	utils_deadband(&adc_after_deadband, DEADZONE, 1.0f);

	commands_printf("Control status:");
	commands_printf("Battery Voltage: %.2f V", (double)GET_INPUT_VOLTAGE());
	commands_printf("ADC Voltage: %.3f V", (double)adc_voltage);
	commands_printf("Set RPM: %.0f RPM", (double)erpm_to_rpm((double)set_erpm_custom));
	commands_printf("Actual RPM: %.0f RPM", (double)erpm_to_rpm((double)mc_interface_get_rpm()));
	commands_printf("Set Current: %.2f A", (double)set_current);
	commands_printf("Motor Current: %.2f A", (double)mc_interface_get_tot_current_filtered());
	commands_printf("P: %.7f\tI: %.7f\tD: %.7f", (double)speed_pid.kp, (double)speed_pid.ki, (double)speed_pid.kd);

	mc_fault_code fault = mc_interface_get_fault();
	if (fault != FAULT_CODE_NONE) {
		commands_printf("FAULT: %d", (int)fault);
	} else {
		commands_printf("Status: OK");
	}
}

static void terminal_set_kp(int argc, const char **argv) {
	if (argc == 2) {
		float new_kp = strtof(argv[1], NULL);
		if (new_kp >= 0.0f && new_kp <= 1.0f) {
			speed_pid.kp = new_kp;
			speed_pid.integrator = 0.0f;
			speed_pid.prev_error = 0.0f;
			speed_pid.differentiator = 0.0f;
			commands_printf("Kp set: %.7f", (double)new_kp);
		} else {
			commands_printf("Error: Kp must be between 0.0 and 1.0");
		}
	} else {
		commands_printf("Usage: kp/ki/kd <value>");
	}
}

static void terminal_set_ki(int argc, const char **argv) {
	if (argc == 2) {
		float new_ki = strtof(argv[1], NULL);
		if (new_ki >= 0.0f && new_ki <= 1.0f) {
			speed_pid.ki = new_ki;
			speed_pid.integrator = 0.0f;
			speed_pid.prev_error = 0.0f;
			speed_pid.differentiator = 0.0f;
			commands_printf("Ki set to: %.4f", (double)new_ki);
		} else {
			commands_printf("Error: Ki must be between 0.0 and 1.0");
		}
	} else {
		commands_printf("Usage: kp/ki/kd <value>");
	}
}

static void terminal_set_kd(int argc, const char **argv) {
	if (argc == 2) {
		float new_kd = strtof(argv[1], NULL);
		if (new_kd >= 0.0f && new_kd <= 1.0f) {
			speed_pid.kd = new_kd;
			speed_pid.integrator = 0.0f;
			speed_pid.prev_error = 0.0f;
			speed_pid.differentiator = 0.0f;
			commands_printf("Kd set to: %.4f", (double)new_kd);
		} else {
			commands_printf("Error: Kd must be between 0.0 and 1.0");
		}
	} else {
		commands_printf("Usage: kp/ki/kd <value>");
	}
}

static void terminal_reset_pid(int argc, const char **argv) {
	(void)argc;
	(void)argv;

	speed_pid.integrator = 0.0f;
	speed_pid.prev_error = 0.0f;
	speed_pid.differentiator = 0.0f;

	commands_printf("Integrator, Error, Differentiator reset");
}

static void terminal_remote_control(int argc, const char **argv) {
	if (argc == 2) {
		if (strcmp(argv[1], "on") == 0) {
			remote_control_mode = true;
			remote_set_erpm = 0.0f;
			// Reset PID state on mode switch
			speed_pid.integrator = 0.0f;
			speed_pid.prev_error = 0.0f;
			speed_pid.differentiator = 0.0f;
			commands_printf("Remote control: ENABLED");
		} else if (strcmp(argv[1], "off") == 0) {
			remote_control_mode = false;
			remote_set_erpm = 0.0f;
			mc_interface_set_current(0);
			commands_printf("Remote control: DISABLED");
		} else {
			commands_printf("Usage: remote [on/off]");
		}
	} else {
		commands_printf("Remote mode: %s", remote_control_mode ? "ON" : "OFF");
	}
}

static void terminal_set_rpm(int argc, const char **argv) {
	if (argc == 2) {
		if (!remote_control_mode) {
			commands_printf("Error: Enable remote mode first (remote on)");
			return;
		}

		float erpm = strtof(argv[1], NULL);
		const volatile mc_configuration *mcconf = mc_interface_get_configuration();
		float effective_max = (runtime_max_erpm > 0) ? runtime_max_erpm : mcconf->l_max_erpm;

		if (erpm > effective_max) {
			erpm = effective_max;
		} else if (erpm < -effective_max) {
			erpm = -effective_max;
		}

		remote_set_erpm = erpm;
		commands_printf("Set RPM: %.0f RPM", (double)erpm_to_rpm((double)erpm));
	} else {
		commands_printf("Usage: setrpm <erpm>");
	}
}

static void terminal_motor_info(int argc, const char **argv) {
	(void)argc;
	(void)argv;

	const volatile mc_configuration *mcconf = mc_interface_get_configuration();

	commands_printf("Motor configuration:");
	switch (mcconf->motor_type) {
		case MOTOR_TYPE_BLDC:
			commands_printf("Motor Type: BLDC");
			break;
		case MOTOR_TYPE_DC:
			commands_printf("Motor Type: DC");
			break;
		case MOTOR_TYPE_FOC:
			commands_printf("Motor Type: FOC");
			break;
		default:
			commands_printf("Motor Type: Unknown");
	}

	commands_printf("Motor Poles: %d", (int)mcconf->si_motor_poles);
	commands_printf("Max RPM: %.0f", (double)erpm_to_rpm((double)mcconf->l_max_erpm));
	commands_printf("Max Current: %.1f A", (double)mcconf->l_current_max);
	commands_printf("Min Current: %.1f A", (double)mcconf->l_current_min);
	commands_printf("Battery Cutoff Start: %.1f V", (double)mcconf->l_battery_cut_start);
	commands_printf("Battery Cutoff End: %.1f V", (double)mcconf->l_battery_cut_end);
}

static void terminal_set_limit(int argc, const char **argv) {
	if (argc == 3) {
		float value = strtof(argv[2], NULL);

		if (strcmp(argv[1], "maxrpm") == 0) {
			const volatile mc_configuration *mcconf = mc_interface_get_configuration();
			runtime_max_erpm = value * (mcconf->si_motor_poles / 2.0f);
			commands_printf("Runtime Max RPM set: %.0f (Reset on reboot)", (double)value);
		} else if (strcmp(argv[1], "maxcurrent") == 0) {
			runtime_max_current = value;
			commands_printf("Runtime Max Current set: %.1f A (Reset on reboot)", (double)value);
		} else {
			commands_printf("Usage: setlimit [maxrpm/maxcurrent] <value>");
		}
	} else if (argc == 1) {
		const volatile mc_configuration *mcconf = mc_interface_get_configuration();
		commands_printf("Runtime limits:");
		commands_printf("Hardware Max RPM: %.0f", (double)erpm_to_rpm((double)mcconf->l_max_erpm));
		commands_printf("Runtime Max RPM: %.0f %s",
			runtime_max_erpm > 0 ? (double)erpm_to_rpm(runtime_max_erpm) : (double)erpm_to_rpm(mcconf->l_max_erpm),
			runtime_max_erpm > 0 ? "(ACTIVE)" : "(using hardware)");
		commands_printf("Hardware Max Current: %.1f A", (double)mcconf->l_current_max);
		commands_printf("Runtime Max Current: %.1f A %s",
			(double)(runtime_max_current > 0 ? runtime_max_current : mcconf->l_current_max),
			runtime_max_current > 0 ? "(ACTIVE)" : "(using hardware)");
	} else {
		commands_printf("Usage: setlimit [maxrpm/maxcurrent] <value>");
	}
}

// Mirrors terminal command handling for lines received over UART from the ESP32 bridge
static void process_line(const char *s) {
	while (*s == ' ') s++;

	if (!strcasecmp(s, "status")) {
		commands_printf("Status:");
		commands_printf("- V: %.2f V\n- I: %.2f A\n- Duty: %.2f",
			(double)GET_INPUT_VOLTAGE(),
			(double)mc_interface_read_reset_avg_input_current(),
			(double)mc_interface_get_duty_cycle_now());
		commands_printf("RPM Now: %.1f", (double)erpm_to_rpm((double)mc_interface_get_rpm()));
		return;
	}

	if (!strcasecmp(s, "motorinfo")) {
		commands_printf("Motor configuration:");
		uart_printf("Motor configuration:");
		const volatile mc_configuration *mcconf = mc_interface_get_configuration();

		switch (mcconf->motor_type) {
			case MOTOR_TYPE_BLDC:
				commands_printf("Motor Type: BLDC");
				uart_printf("Motor Type: BLDC");
				break;
			case MOTOR_TYPE_DC:
				commands_printf("Motor Type: DC");
				uart_printf("Motor Type: DC");
				break;
			case MOTOR_TYPE_FOC:
				commands_printf("Motor Type: FOC");
				uart_printf("Motor Type: FOC");
				break;
			default:
				commands_printf("Motor Type: Unknown");
				uart_printf("Motor Type: Unknown");
		}

		commands_printf("Motor Poles: %d", (int)mcconf->si_motor_poles);
		commands_printf("Max RPM: %.0f", (double)erpm_to_rpm((double)mcconf->l_max_erpm));
		commands_printf("Max Current: %.1f A", (double)mcconf->l_current_max);
		commands_printf("Min Current: %.1f A", (double)mcconf->l_current_min);
		commands_printf("Battery Cutoff Start: %.1f V", (double)mcconf->l_battery_cut_start);
		commands_printf("Battery Cutoff End: %.1f V", (double)mcconf->l_battery_cut_end);
		uart_printf("Motor Poles: %d", (int)mcconf->si_motor_poles);
		uart_printf("Max RPM: %.0f", (double)erpm_to_rpm((double)mcconf->l_max_erpm));
		uart_printf("Max Current: %.1f A", (double)mcconf->l_current_max);
		uart_printf("Min Current: %.1f A", (double)mcconf->l_current_min);
		uart_printf("Battery Cutoff Start: %.1f V", (double)mcconf->l_battery_cut_start);
		uart_printf("Battery Cutoff End: %.1f V", (double)mcconf->l_battery_cut_end);
		return;
	}

	if (!strncasecmp(s, "remote ", 7)) {
		const char *p = s + 7;
		if (!strncasecmp(p, "on", 2)) {
			remote_control_mode = true;
			remote_set_erpm = 0.0f;
			commands_printf("Remote control: ENABLED");
		} else if (!strncasecmp(p, "off", 3)) {
			remote_control_mode = false;
			remote_set_erpm = 0.0f;
			mc_interface_set_current(0.0f);
			commands_printf("Remote control: DISABLED");
		} else {
			commands_printf("Usage: remote [on/off]");
		}
		return;
	}

	if (!strncasecmp(s, "setrpm ", 7)) {
		float erpm = (float)atof(s + 7);
		const volatile mc_configuration *mcconf = mc_interface_get_configuration();
		float eff = (runtime_max_erpm > 0.0f) ? runtime_max_erpm : mcconf->l_max_erpm;
		if (erpm > eff) erpm = eff;
		if (erpm < -eff) erpm = -eff;
		remote_set_erpm = erpm;
		commands_printf("Set RPM: %.0f RPM", (double)erpm_to_rpm((double)erpm));
		return;
	}

	if (!strncasecmp(s, "kp ", 3)) {
		speed_pid.kp = (float)atof(s + 3);
		commands_printf("Kp=%.6f", (double)speed_pid.kp);
		return;
	}
	if (!strncasecmp(s, "ki ", 3)) {
		speed_pid.ki = (float)atof(s + 3);
		commands_printf("Ki=%.6f", (double)speed_pid.ki);
		return;
	}
	if (!strncasecmp(s, "kd ", 3)) {
		speed_pid.kd = (float)atof(s + 3);
		commands_printf("Kd=%.6f", (double)speed_pid.kd);
		return;
	}

	if (!strcasecmp(s, "reset")) {
		speed_pid.integrator = 0.0f;
		speed_pid.prev_error = 0.0f;
		speed_pid.differentiator = 0.0f;

		commands_printf("Integrator, Error, Differentiator reset");
		uart_printf("Integrator, Error, Differentiator reset"); // echoed to UART so the host app sees it too
		return;
	}

	if (!strncasecmp(s, "setlimit", 8)) {
		const char *p = s + 8;
		while (*p == ' ') p++;

		if (!*p) {
			const volatile mc_configuration *mcconf = mc_interface_get_configuration();
			commands_printf("Runtime limits:");
			commands_printf("Hardware Max RPM: %.0f", (double)erpm_to_rpm((double)mcconf->l_max_erpm));
			commands_printf("Runtime Max RPM: %.0f %s",
				(double)(runtime_max_erpm > 0 ? (double)erpm_to_rpm(runtime_max_erpm) : (double)erpm_to_rpm(mcconf->l_max_erpm)),
				runtime_max_erpm > 0 ? "(ACTIVE)" : "(using hardware)");
			commands_printf("Hardware Max Current: %.1f A", (double)mcconf->l_current_max);
			commands_printf("Runtime Max Current: %.1f A %s",
				(double)(runtime_max_current > 0 ? runtime_max_current : mcconf->l_current_max),
				runtime_max_current > 0 ? "(ACTIVE)" : "(using hardware)");
			uart_printf("Runtime limits:");
			uart_printf("Hardware Max RPM: %.0f", (double)erpm_to_rpm((double)mcconf->l_max_erpm));
			uart_printf("Runtime Max RPM: %.0f %s",
				(double)(runtime_max_erpm > 0 ? (double)erpm_to_rpm(runtime_max_erpm) : (double)erpm_to_rpm(mcconf->l_max_erpm)),
				runtime_max_erpm > 0 ? "(ACTIVE)" : "(using hardware)");
			uart_printf("Hardware Max Current: %.1f A", (double)mcconf->l_current_max);
			uart_printf("Runtime Max Current: %.1f A %s",
				(double)(runtime_max_current > 0 ? runtime_max_current : mcconf->l_current_max),
				runtime_max_current > 0 ? "(ACTIVE)" : "(using hardware)");
			return;
		}

		if (!strncasecmp(p, "maxrpm ", 7)) {
			float rpm = (float)atof(p + 7);
			const volatile mc_configuration *mcconf = mc_interface_get_configuration();
			float erpm = rpm * (mcconf->si_motor_poles / 2.0f);
			runtime_max_erpm = erpm;
			commands_printf("Runtime Max RPM set: %.0f", (double)rpm);
			return;
		}
		if (!strncasecmp(p, "maxcurrent ", 11)) {
			runtime_max_current = (float)atof(p + 11);
			commands_printf("Runtime Max Current set: %.1f A", (double)runtime_max_current);
			return;
		}
		commands_printf("Usage: setlimit [maxrpm/maxcurrent] <value>");
		return;
	}

	commands_printf("Unknown cmd: %s", s);
}

static float erpm_to_rpm(float erpm) {
	const volatile mc_configuration *mcconf = mc_interface_get_configuration();
	int poles = mcconf->si_motor_poles;
	if (poles <= 0) return 0.0f;
	return erpm / (poles / 2);
}

static void uart_printf(const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);

	char buffer[256];
	int len = vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);

	if (len > 0) {
		sdWrite(&HW_UART_DEV, (uint8_t*)buffer, len);
		sdWrite(&HW_UART_DEV, (uint8_t*)"\n", 1);
	}
}