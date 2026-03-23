#pragma once

#include "esp_err.h"
#include <stdbool.h>

/*
 * Minimal serial CLI used as a stable configuration and recovery shell when the
 * full esp_console REPL is unavailable on this hardware build.
 */
esp_err_t recovery_cli_start(void);
void recovery_cli_print_banner(void);
