#include "app.h"
#include "ch.h"
#include "hal.h"

// VESC
#include "mc_interface.h"
#include "utils_math.h"
#include "timeout.h"
#include "commands.h"
#include "terminal.h"

// Configuration
#define VOLT_CENTER         1.65f
#define VOLT_MIN            0.4f
#define VOLT_MAX            3.1f
#define DEADZONE            0.1f
#define UPDATE_RATE_HZ      50
#define FILTER_SAMPLES      3

static THD_FUNCTION(basic_adc_thread, arg);
static THD_WORKING_AREA(basic_adc_thread_wa, 512);

static float read_adc(void);
static void terminal_adc_status(int argc, const char **argv);

static volatile bool stop_now = true;
static volatile bool is_running = false;
static volatile float adc_voltage = 0.0;
static volatile float adc_normalized = 0.0;
static volatile float set_rpm = 0.0;

void app_custom_start(void) {
#ifdef HW_ADC_EXT_GPIO
	palSetPadMode(HW_ADC_EXT_GPIO, HW_ADC_EXT_PIN, PAL_MODE_INPUT_ANALOG);
#endif
	stop_now = false;

	terminal_register_command_callback(
			"adc_status",
			"Show ADC status",
			"",
			terminal_adc_status);
	chThdCreateStatic(basic_adc_thread_wa, sizeof(basic_adc_thread_wa), NORMALPRIO, basic_adc_thread, NULL);
}

// Stops the thread and releases callbacks
void app_custom_stop(void) {
	terminal_unregister_callback(terminal_adc_status);

	stop_now = true;
	while (is_running) {
		chThdSleepMilliseconds(1);
	}
}

void app_custom_configure(app_configuration *conf) {
	(void)conf;
}

static THD_FUNCTION(basic_adc_thread, arg) {
	(void)arg;

	chRegSetThreadName("APP_ADC_BASIC");
	is_running = true;
	systime_t sleep_time = CH_CFG_ST_FREQUENCY / UPDATE_RATE_HZ;

	// At least one tick should be slept to not block the other threads
	if (sleep_time == 0) {
		sleep_time = 1;
	}

	for (;;) {
		chThdSleep(sleep_time);
		if (stop_now) {
			is_running = false;
			return;
		}

		if (mc_interface_get_fault() != FAULT_CODE_NONE) {
			mc_interface_set_current(0);
		}

		if (app_is_output_disabled()) {
			continue;
		}

		float adc_signal = read_adc();
		adc_normalized = adc_signal;
		utils_deadband(&adc_signal, DEADZONE, 1.0f);

		const volatile mc_configuration *mcconf = mc_interface_get_configuration();
		float max_erpm = mcconf->l_max_erpm;
		float min_erpm = mcconf->l_min_erpm;

		if (adc_signal >= 0.0f) {
			set_rpm = adc_signal * max_erpm;
		} else {
			set_rpm = adc_signal * fabsf(min_erpm);
		}

		mc_interface_set_pid_speed(set_rpm);
		timeout_reset();
	}
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

	return normalized;
}

static void terminal_adc_status(int argc, const char **argv) {
	(void)argc;
	(void)argv;

	float adc_after_deadband = adc_normalized;
	utils_deadband(&adc_after_deadband, DEADZONE, 1.0f);

	commands_printf("ADC RPM control status:");
	commands_printf("ADC Voltage: %.3f V", (double)adc_voltage);
	commands_printf("Normalized Signal: %.3f", (double)adc_normalized);
	commands_printf("After Deadband: %.3f", (double)adc_after_deadband);
	commands_printf("Target RPM: %.0f ERPM", (double)set_rpm);
	commands_printf("Actual RPM: %.0f ERPM", (double)mc_interface_get_rpm());
	commands_printf("Motor Current: %.2f A", (double)mc_interface_get_tot_current_filtered());
	commands_printf("Battery Voltage: %.2f V", (double)GET_INPUT_VOLTAGE());

	mc_fault_code fault = mc_interface_get_fault();
	if (fault != FAULT_CODE_NONE) {
		commands_printf("FAULT: %d", (int)fault);
	} else {
		commands_printf("Status: OK");
	}
}