#include "app.h"
#include "ch.h"
#include "terminal.h"
#include "commands.h"

// Private variables
static volatile bool stop_now = true;
static volatile bool is_running = false;

// Private function
static void terminal_hello(int arrgc, const char **argv);

void app_custom_start(void) {

	stop_now = false;
	is_running = true;

	// Terminal commands for the VESC Tool terminal can be registered.
	terminal_register_command_callback(
			"print_hello",
			"Print the Hello",
			"",
			terminal_hello);
}

// Called when the custom application is stopped. Stop our threads
// and release callbacks.
void app_custom_stop(void) {

	terminal_unregister_callback(terminal_hello);

	stop_now = true;
	is_running = false;
}

void app_custom_configure(app_configuration *conf) {
	(void)conf;
}

static void terminal_hello(int argc, const char **argv){
	(void) argc;
	(void) argv;

	commands_printf("Hello I'm VESC, you custom successfully");
}
