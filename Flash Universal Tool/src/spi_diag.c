#include "spi_diag.h"
#include "globals.h"
#include "spi_ops.h"
#include "flash_info.h"
#include "json.h"
#include "hardware/gpio.h"
#include <stdlib.h>
#include <stdio.h>

// Run full SPI diagnostic and generate JSON report
bool run_spi_diagnostic(char *json_out, size_t json_cap) {
    if (!spi_initialized) {
        snprintf(json_out, json_cap, "{\"error\":\"SPI not initialized\"}");
        return false;
    }

    mutex_enter_blocking(&spi_mutex);

    // Get expected report size
    size_t expected = get_expected_report_size();
    if (expected == 0) {
        mutex_exit(&spi_mutex);
        snprintf(json_out, json_cap, "{\"error\":\"No commands defined\"}");
        return false;
    }

    // Allocate buffer for raw SPI data
    uint8_t *report = malloc(expected);
    if (!report) {
        mutex_exit(&spi_mutex);
        snprintf(json_out, json_cap, "{\"error\":\"Memory allocation failed\"}");
        return false;
    }

    // Execute safe operation transfer
    int stored = spi_OPSAFE_transfer(SPI_PORT, report, expected);

    if (stored <= 0) {
        free(report);
        mutex_exit(&spi_mutex);
        snprintf(json_out, json_cap, "{\"error\":\"SPI transfer failed\"}");
        return false;
    }

    // Store JEDEC ID for quick reference
    if (stored >= 3) {
        last_jedec_id[0] = report[0];
        last_jedec_id[1] = report[1];
        last_jedec_id[2] = report[2];
    }

    // Generate JSON from raw data
    size_t written = json_export_full_report(json_out, json_cap, report, stored);

    free(report);
    mutex_exit(&spi_mutex);

    return (written > 0);
}

// Quick JEDEC ID read
bool read_jedec_id(uint8_t *mfr, uint8_t *mem_type, uint8_t *capacity) {
    if (!spi_initialized) {
        return false;
    }

    mutex_enter_blocking(&spi_mutex);

    const opcode *jedec_cmd = get_command_by_index(0); // JEDEC is first command
    if (!jedec_cmd) {
        mutex_exit(&spi_mutex);
        return false;
    }

    uint8_t tx_buffer[1];
    uint8_t rx_buffer[3];

    int result = spi_ONE_transfer(SPI_PORT, *jedec_cmd, tx_buffer, rx_buffer);
    gpio_put(CS_PIN, 1);

    mutex_exit(&spi_mutex);

    if (result == 3) {
        *mfr = rx_buffer[0];
        *mem_type = rx_buffer[1];
        *capacity = rx_buffer[2];

        last_jedec_id[0] = rx_buffer[0];
        last_jedec_id[1] = rx_buffer[1];
        last_jedec_id[2] = rx_buffer[2];

        return true;
    }

    return false;
}