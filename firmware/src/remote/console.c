
#include "config.h"
#include "driver/usb_serial_jtag.h"
#include "esp_console.h"
#include "esp_core_dump.h"
#include "esp_log.h"
#include "powermanagement.h"
#include "remoteinputs.h"
#include "settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "linenoise/linenoise.h"

// https://github.com/espressif/esp-idf/blob/master/examples/system/console/basic/main/console_example_main.c

static const char *TAG = "PUBREMOTE-CONSOLE";
#define PROMPT_STR "pubconsole"

static int get_version() {
  printf("version: %d.%d.%d\n", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);
  printf("variant: %s\n", RELEASE_VARIANT);
  printf("hardware: %s\n", HW_TYPE);
  printf("build_date: %s %s\n", __DATE__, __TIME__);
  printf("build_id: %s\n", BUILD_ID);
  return 0;
}

static void register_version_command() {
  esp_console_cmd_t cmd = {
      .command = "version",
      .help = "Get information about the current firmware build.\n"
              "This command returns the version number, build date, and build ID.",
      .hint = NULL,
      .func = &get_version,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

static int reboot_command() {
  ESP_LOGI(TAG, "Rebooting...");
  esp_restart();
  return 0;
}

static void register_reboot_command() {
  esp_console_cmd_t cmd = {
      .command = "reboot",
      .help = "Reboot the device",
      .hint = NULL,
      .func = &reboot_command,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

static int shutdown_command() {
  enter_sleep();
  return 0;
}

static void register_shutdown_command() {
  esp_console_cmd_t cmd = {
      .command = "shutdown",
      .help = "Shutdown the device and enter sleep mode",
      .hint = NULL,
      .func = &shutdown_command,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

static int erase_command() {
  ESP_LOGI(TAG, "Erasing flash memory...");
  esp_err_t err = reset_all_settings();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to erase flash: %s", esp_err_to_name(err));
    return -1;
  }
  ESP_LOGI(TAG, "Flash memory erased successfully.");
  esp_restart();
  return 0;
}

static void register_erase_command() {
  esp_console_cmd_t cmd = {
      .command = "erase",
      .help = "Erase the flash memory",
      .hint = NULL,
      .func = &erase_command,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

// Comma separated, so the web tool needs no per-chip pin tables
static void print_pin_list(const char *label, uint64_t mask) {
  printf("%s: ", label);
  bool first = true;
  for (int io_num = 0; io_num < 64; io_num++) {
    if (mask & (1ULL << io_num)) {
      printf(first ? "%d" : ",%d", io_num);
      first = false;
    }
  }
  printf("\n");
}

static void print_pin_warnings(void) {
  char warn[192];
  if (input_pins_warnings(&input_pin_settings, warn, sizeof(warn)) > 0) {
    printf("pins_warning: %s\n", warn);
  }
}

static int get_settings(int argc, char **argv) {
  if (argc > 1) {
    ESP_LOGE(TAG, "Usage: settings");
    return -1;
  }
  else {
    char *current_ssid = get_wifi_ssid();
    char *current_password = get_wifi_password();

    if (current_ssid == NULL) {
      current_ssid = "\0";
    }
    if (current_password == NULL) {
      current_password = "\0";
    }

    printf("WiFi Credentials:\n");
    printf("wifi_ssid: %s\n", current_ssid);
    printf("wifi_password: %s\n", current_password);

    printf("Input Pins:\n");
    printf("js_x_gpio: %d\n", input_pin_settings.js_x_gpio);
    printf("js_y_gpio: %d\n", input_pin_settings.js_y_gpio);
    printf("btn1_gpio: %d\n", input_pin_settings.btn1_gpio);
    printf("btn1_level: %u\n", input_pin_settings.btn1_active_level);
    uint64_t assignable = input_pins_assignable_mask();
    print_pin_list("pins_available", assignable);
    print_pin_list("pins_adc_capable", input_pins_adc_capable_mask() & assignable);
    print_pin_list("pins_button_capable", input_pins_button_capable_mask());
    print_pin_list("pins_reserved", input_pins_reserved_mask());
    print_pin_warnings();
  }

  return 0;
}

static void register_get_settings_command() {
  esp_console_cmd_t cmd = {
      .command = "settings",
      .help = "Get current device settings",
      .hint = NULL,
      .func = &get_settings,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

// A GPIO number, or -1 to disable the input
static bool parse_pin_value(const char *value, int8_t *out, const char **err) {
  char *end = NULL;
  long parsed = strtol(value, &end, 10);

  if (end == value || *end != '\0') {
    *err = "expected a GPIO number";
    return false;
  }
  if (parsed < INPUT_PIN_DISABLED || parsed >= 64) {
    *err = "out of range (-1 to disable)";
    return false;
  }

  *out = (int8_t)parsed;
  return true;
}

static int save_settings_command(int argc, char **argv) {
  if (argc < 3 || argc % 2 == 0) {
    ESP_LOGE(TAG, "Usage: save_settings <SETTING1> <VALUE2> <SETTING2> <VALUE2>...");
    return -1;
  }
  else {
    int setting_count = (argc - 1) / 2;
    // Collected first and applied once, so a multi-pin remap is atomic
    InputPinSettings pending_pins = input_pin_settings;
    bool pins_dirty = false;

    for (int i = 0; i < setting_count; i++) {
      const char *setting = argv[1 + i * 2];
      const char *value = argv[2 + i * 2];
      const char *pin_err = NULL;

      if (strcmp(setting, "js_x_gpio") == 0) {
        if (!parse_pin_value(value, &pending_pins.js_x_gpio, &pin_err)) {
          printf("pins_error: js_x_gpio %s\n", pin_err);
          return -1;
        }
        pins_dirty = true;
      }
      else if (strcmp(setting, "js_y_gpio") == 0) {
        if (!parse_pin_value(value, &pending_pins.js_y_gpio, &pin_err)) {
          printf("pins_error: js_y_gpio %s\n", pin_err);
          return -1;
        }
        pins_dirty = true;
      }
      else if (strcmp(setting, "btn1_gpio") == 0) {
        if (!parse_pin_value(value, &pending_pins.btn1_gpio, &pin_err)) {
          printf("pins_error: btn1_gpio %s\n", pin_err);
          return -1;
        }
        pins_dirty = true;
      }
      else if (strcmp(setting, "btn1_level") == 0) {
        if (strcmp(value, "0") != 0 && strcmp(value, "1") != 0) {
          printf("pins_error: btn1_level must be 0 or 1\n");
          return -1;
        }
        pending_pins.btn1_active_level = (value[0] == '1') ? 1 : 0;
        pins_dirty = true;
      }
      else if (strcmp(setting, "wifi_ssid") == 0) {
        if (strlen(value) > 32) {
          ESP_LOGE(TAG, "SSID must be less than 33 characters.");
          return -1;
        }
        else {
          ESP_LOGI(TAG, "Saving Wifi SSID: %s", value);
          esp_err_t err = save_wifi_ssid(value);
          if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save WiFi SSID: %s", esp_err_to_name(err));
            return -1;
          }
          ESP_LOGI(TAG, "Setting SSID: %s", value);
          // save_settings wifi_ssid SiWi wifi_password internetgo
        }
      }
      else if (strcmp(setting, "wifi_password") == 0) {
        if (strlen(value) > 64) {
          ESP_LOGE(TAG, "Password must be less than 65 characters.");
          return -1;
        }
        else {
          ESP_LOGI(TAG, "Saving WiFi Password: %s", value);
          esp_err_t err = save_wifi_password(value);
          if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save WiFi password: %s", esp_err_to_name(err));
            return -1;
          }
          ESP_LOGI(TAG, "Setting Password: %s", value);
        }
      }
      else {
        ESP_LOGE(TAG, "Unknown setting: %s", setting);
        return -1;
      }
    }

    if (pins_dirty) {
      char apply_err[128] = {0};
      esp_err_t err = input_pins_apply(&pending_pins, apply_err, sizeof(apply_err));
      if (err != ESP_OK) {
        printf("pins_error: %s\n", apply_err[0] != '\0' ? apply_err : esp_err_to_name(err));
        return -1;
      }
      printf("pins_applied: ok\n");
      print_pin_warnings();
    }

    return 0;
  }
}

static void register_save_settings_command() {
  esp_console_cmd_t cmd = {
      .command = "save_settings",
      .help = "Save remote settings.\n"
              "Supported: wifi_ssid, wifi_password, js_x_gpio, js_y_gpio, btn1_gpio, btn1_level.\n"
              "Pin changes apply immediately (-1 disables an input).",
      .hint = "save_settings <SETTING1> <VALUE1> <SETTING2> <VALUE2>...",
      .func = &save_settings_command,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

static int crash_command() {
  printf("Forcing a crash for testing backtrace decoding...\n");
  int *ptr = NULL;
  *ptr = 42; // Dereference NULL pointer to cause a crash
  return 0;
}

static void register_crash_command() {
  esp_console_cmd_t cmd = {
      .command = "crash",
      .help = "Force a crash to test backtrace decoding",
      .hint = NULL,
      .func = &crash_command,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

static esp_err_t check_and_validate_coredump() {
  esp_err_t err = esp_core_dump_image_check();
  if (err != ESP_OK) {
    if (err == ESP_ERR_INVALID_SIZE || err == ESP_ERR_INVALID_CRC) {
        printf("coredump: corrupt (err=%d), erasing...\n", err);
        esp_core_dump_image_erase();
    }
    return err;
  }
  return ESP_OK;
}

static int coredump_info_command() {
  if (check_and_validate_coredump() != ESP_OK) {
    printf("coredump: none\n");
    return 0;
  }

  esp_core_dump_summary_t *summary = malloc(sizeof(esp_core_dump_summary_t));
  if (summary == NULL) {
    printf("Failed to allocate memory for core dump summary\n");
    return -1;
  }
  esp_err_t err = esp_core_dump_get_summary(summary);
  if (err == ESP_OK) {
    printf("coredump: found\n");
  }
  else {
    printf("coredump: none\n");
  }
  free(summary);
  return 0;
}

static void register_coredump_info_command() {
  esp_console_cmd_t cmd = {
      .command = "coredump_info",
      .help = "Check for existence of a core dump",
      .hint = NULL,
      .func = &coredump_info_command,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

static int coredump_print_command() {
  if (check_and_validate_coredump() != ESP_OK) {
    printf("coredump: none\n");
    return 0;
  }

  esp_core_dump_summary_t *summary = malloc(sizeof(esp_core_dump_summary_t));
  if (summary == NULL) {
    printf("Failed to allocate memory for core dump summary\n");
    return -1;
  }
  esp_err_t err = esp_core_dump_get_summary(summary);
  if (err == ESP_OK) {
    printf("coredump_task: %s\n", summary->exc_task);
    printf("coredump_backtrace:\n");
    printf("Backtrace:");
    for (int i = 0; i < summary->exc_bt_info.depth; i++) {
      printf(" 0x%lx", summary->exc_bt_info.bt[i]);
    }
    printf("\n");
  }
  else {
    printf("No core dump found or failed to parse (err=%d)\n", err);
  }
  free(summary);
  return 0;
}

static void register_coredump_print_command() {
  esp_console_cmd_t cmd = {
      .command = "coredump_print",
      .help = "Print the backtrace from the stored core dump",
      .hint = NULL,
      .func = &coredump_print_command,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

static int coredump_erase_command() {
  esp_err_t err = esp_core_dump_image_erase();
  if (err == ESP_OK) {
    printf("Core dump erased successfully\n");
  }
  else {
    printf("Failed to erase core dump (err=%d)\n", err);
  }
  return 0;
}

static void register_coredump_erase_command() {
  esp_console_cmd_t cmd = {
      .command = "coredump_erase",
      .help = "Erase the stored core dump",
      .hint = NULL,
      .func = &coredump_erase_command,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}


static int complete_command(int argc, char **argv) {
  if (argc != 2) {
    ESP_LOGE(TAG, "Usage: complete <prefix>");
    return -1;
  }
  
  linenoiseCompletions lc = {0};
  esp_console_get_completion(argv[1], &lc);
  
  for (size_t i = 0; i < lc.len; i++) {
    printf("%s\n", lc.cvec[i]);
  }
  
  for (size_t i = 0; i < lc.len; i++) {
    free(lc.cvec[i]);
  }
  if (lc.cvec) {
    free(lc.cvec);
  }
  return 0;
}

static void register_complete_command() {
  esp_console_cmd_t cmd = {
      .command = "complete",
      .help = "Get autocompletion for a prefix",
      .hint = NULL,
      .func = &complete_command,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

static esp_console_repl_t *s_repl = NULL;

static void console_start(void) {
  ESP_LOGI(TAG, "Initializing console");
  esp_console_repl_t *repl = NULL;
  esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
  repl_config.task_stack_size = 3072; // Reduce from default 4096
  /* Prompt to be printed before each line.
   * This can be customized, made dynamic, etc.
   */
  repl_config.prompt = PROMPT_STR ">";
  /* Register commands */
  esp_console_register_help_command();
  register_version_command();
  register_reboot_command();
  register_shutdown_command();
  register_erase_command();
  register_get_settings_command();
  register_save_settings_command();
  register_crash_command();
  register_coredump_info_command();
  register_coredump_print_command();
  register_coredump_erase_command();
  register_complete_command();

#if defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
  esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
  esp_err_t err = esp_console_new_repl_uart(&hw_config, &repl_config, &repl);

#elif defined(CONFIG_ESP_CONSOLE_USB_CDC)
  esp_console_dev_usb_cdc_config_t hw_config = ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
  esp_err_t err = esp_console_new_repl_usb_cdc(&hw_config, &repl_config, &repl);

#elif defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
  esp_console_dev_usb_serial_jtag_config_t hw_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
  esp_err_t err = esp_console_new_repl_usb_serial_jtag(&hw_config, &repl_config, &repl);

#else
  #error Unsupported console type
#endif

  if (err != ESP_OK) {
    esp_console_deinit();
    ESP_LOGE(TAG, "Failed to initialize console REPL (err: %s). Insufficient memory?", esp_err_to_name(err));
    return;
  }

  linenoiseSetCompletionCallback(esp_console_get_completion);
  linenoiseSetHintsCallback((linenoiseHintsCallback*)esp_console_get_hint);
  linenoiseSetFreeHintsCallback(free);

  err = esp_console_start_repl(repl);
  if (err != ESP_OK) {
    repl->del(repl);
    ESP_LOGE(TAG, "Failed to start console REPL (err: %s).", esp_err_to_name(err));
    return;
  }
  s_repl = repl;
  ESP_LOGI(TAG, "Console initialized");
}

static void console_stop(void) {
  if (!s_repl) {
    return;
  }
  // Tears down the REPL task, the command list and the USB driver, and puts the VFS back in
  // non-blocking mode so ESP_LOG keeps working without the console.
  esp_err_t err = s_repl->del(s_repl);
  s_repl = NULL;
  ESP_LOGI(TAG, "Console stopped (err: %s)", esp_err_to_name(err));
}

// The REPL is worth its internal RAM only while something is listening. SOF packets come
// from a real host, so a charger or power bank reads as disconnected.
void console_poll_usb(void) {
#if defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
  static int agree = 0;
  bool connected = usb_serial_jtag_is_connected();
  if (connected == (s_repl != NULL)) {
    agree = 0;
    return;
  }
  // Debounced, so a cable settling does not install and uninstall the driver
  if (++agree < 4) {
    return;
  }
  agree = 0;
  if (connected) {
    console_start();
  }
  else {
    console_stop();
  }
#endif
}

void console_init() {
#if defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
  // Straight to console_start, not through the debounce - at boot there is nothing to settle
  if (usb_serial_jtag_is_connected()) {
    console_start();
  }
  else {
    ESP_LOGI(TAG, "No USB host attached; console deferred");
  }
#else
  console_start();
#endif
}
