#include "cli.h"
#include "globals.h"
#include "flash_ops.h"
#include "spi_diag.h"
#include "spi_ops.h"
#include "flash_info.h"
#include "json.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

// ========== CLI Helper functions ==========

void clear_screen(void) { printf("\033[2J\033[H"); }

void print_header(const char *title) {
    printf("\n");
    printf("╔════════════════════════════════════════╗\n");
    printf("║ %-38s ║\n", title);
    printf("╚════════════════════════════════════════╝\n");
}

void print_separator(void) {
    printf("──────────────────────────────────────────\n");
}

void print_section(const char *section_name) {
    printf("\n┌─ %s\n", section_name);
}

void get_input_line(char *buffer, int max_len) {
    int i = 0;
    char c;
    while (i < max_len - 1) {
        c = getchar();
        if (c == '\r' || c == '\n') {
            printf("\n");
            break;
        }
        if (c == '\b' || c == 0x7F) { // Backspace
            if (i > 0) {
                printf("\b \b");
                i--;
            }
        } else {
            printf("%c", c);
            buffer[i++] = c;
        }
    }
    buffer[i] = '\0';
}

uint32_t get_hex_input(const char *prompt) {
    char buf[32];
    printf("%s", prompt);
    get_input_line(buf, 32);
    return (uint32_t)strtoul(buf, NULL, 0); // 0 base handles 0x prefix or decimal
}

bool confirm_destructive(const char *action) {
    printf("\n⚠️  WARNING: %s\n", action);
    printf("This operation is DESTRUCTIVE. Continue? (y/n): ");
    char c;
    do {
        c = getchar();
    } while (c == '\n' || c == '\r');
    printf("%c\n", c);
    return (c == 'y' || c == 'Y');
}

void print_report_buffer_formatted(const uint8_t *buf, size_t len) {
    clear_screen();
    print_header("FULL CHIP REPORT");

    size_t offset = 0;
    size_t num_commands = get_safe_command_count();

    for (size_t i = 0; i < num_commands; i++) {
        const opcode *cmd = get_command_by_index(i);
        if (cmd == NULL)
            continue;

        char section_title[60];
        snprintf(section_title, sizeof(section_title), "%.60s [Opcode: 0x%02X]",
                 cmd->description, cmd->opcode);
        print_section(section_title);

        if (cmd->opcode == 0x9F) {
            if (offset + cmd->rx_data_len <= len) {
                uint8_t mfr_id = buf[offset];
                uint8_t mem_type = buf[offset + 1];
                uint8_t capacity = buf[offset + 2];

                int valid = decode_jedec_id(mfr_id, mem_type, capacity);

                if (valid) {
                    print_jedec_report(mfr_id, mem_type, capacity);
                } else {
                    print_section("JEDEC ID Analysis");
                    printf("| ERROR - Invalid JEDEC ID Response\n");
                    print_separator();
                }
            }
        } else if (cmd->opcode == 0x5A && cmd->rx_data_len == 8) {
            decode_sfdp_header(&buf[offset]);
        } else if (cmd->opcode == 0x5A && cmd->rx_data_len == 24) {
            decode_sfdp_param_headers(&buf[offset]);
        } else {
            printf("│ Data: ");
            for (size_t j = 0; j < cmd->rx_data_len; j++) {
                if (offset + j < len) {
                    printf("0x%02X ", buf[offset + j]);
                }
            }
            printf("\n");
        }

        if (i > 0 && (i % 3) == 2) {
            printf("\nPress any key to continue...");
            char c;
            do {
                c = getchar();
            } while (c == PICO_ERROR_TIMEOUT);
            clear_screen();

            size_t sections_per_page = 3;
            size_t current_page = (i / sections_per_page) + 1;
            size_t total_pages =
                (num_commands + sections_per_page - 1) / sections_per_page;

            char page_title[50];
            snprintf(page_title, sizeof(page_title),
                     "FULL CHIP REPORT (Page %zu / %zu)", current_page, total_pages);

            print_header(page_title);
        }

        offset += cmd->rx_data_len;
    }

    print_separator();
    printf("Total useful bytes read: %zu bytes\n", offset);
    print_separator();
}

void print_individual_command(const char *name, uint8_t opcode,
                              const uint8_t *rx_buffer, size_t total_len,
                              size_t data_start, size_t data_len) {
    clear_screen();
    print_header(name);

    printf("\nOpcode: 0x%02X | Total Length: %zu bytes\n", opcode, total_len);
    print_separator();

    printf("\n┌─ Raw Response (Full %zu bytes)\n", total_len);
    for (size_t i = 0; i < total_len; i++) {
        if (i % 8 == 0)
            printf("│ ");
        printf("0x%02X ", rx_buffer[i]);
        if ((i + 1) % 8 == 0)
            printf("\n");
    }
    if (total_len % 8 != 0)
        printf("\n");

    print_section("Data Breakdown");
    printf("│ Junk (Protocol) : ");
    for (size_t i = 0; i < data_start; i++) {
        printf("0x%02X ", rx_buffer[i]);
    }
    printf("\n");

    printf("│ Payload Data    : ");
    bool all_zero = true;
    bool all_ff = true;

    for (size_t i = data_start; i < data_start + data_len; i++) {
        printf("0x%02X ", rx_buffer[i]);
        if (rx_buffer[i] != 0x00)
            all_zero = false;
        if (rx_buffer[i] != 0xFF)
            all_ff = false;
    }
    printf("\n");

    print_section("Assessment");
    printf("│ ");
    if (all_ff) {
        printf("ERROR - All 0xFF (no device response)\n");
    } else if (all_zero && data_len > 1) {
        printf("WARNING - All zeros (stuck low or unprogrammed)\n");
    } else if (opcode == 0x9F && rx_buffer[data_start] != 0xFF) {
        printf("VALID - JEDEC ID response received\n");
    } else {
        printf("VALID - Response received\n");
    }

    print_separator();
}

char get_menu_choice(void) {
    char c;
    do {
        c = getchar();
    } while (c == PICO_ERROR_TIMEOUT || c == '\n' || c == '\r');
    printf("%c\n", c);
    return c;
}

void print_main_menu(void) {
    clear_screen();
    printf("\n");
    printf("╔════════════════════════════════════════╗\n");
    printf("║                                        ║\n");
    printf("║           SPI Flash Identifier         ║\n");
    printf("║                v2.1                    ║\n");
    printf("║                                        ║\n");
    printf("╚════════════════════════════════════════╝\n");
    printf("\n");
    printf("  [1] Identify Chip (Quick JEDEC ID)\n");
    printf("  [2] Full Safe Read Report\n");
    printf("  [3] Individual Safe Commands\n");
    printf("  [4] Export Full Safe Report in JSON\n");
    printf("  --------------------------------\n");
    printf("  [5] READ Flash (Raw Bytes)\n");
    printf("  [6] WRITE Flash (Text String)\n");
    printf("  [7] ERASE Flash (Sector Aligned)\n");
    printf("──────────────────────────────────────────\n");
    printf("  [8] Opcode Fuzzing (Dangerous)\n");
    printf("──────────────────────────────────────────\n");
}

void cli_core(void) {
    sleep_ms(2000);
    printf("\n[CLI] Starting SPI Flash Tool CLI on Core 1...\n");

    while (true) {
        print_main_menu();
        printf("Enter your choice: ");
        char choice = get_menu_choice();

        switch (choice) {
        case '1': {
            clear_screen();
            print_header("CHIP IDENTIFICATION");
            printf("\nReading JEDEC ID (0x9F)...\n");
            print_separator();

            uint8_t tx_buffer[1];
            uint8_t rx_buffer[3];
            const opcode *jedec_cmd = get_command_by_index(0);

            mutex_enter_blocking(&spi_mutex);
            int result = spi_ONE_transfer(SPI_PORT, *jedec_cmd, tx_buffer, rx_buffer);
            gpio_put(CS_PIN, 1);
            mutex_exit(&spi_mutex);

            if (result == 3) {
                printf("Raw JEDEC: %02X %02X %02X\n", rx_buffer[0], rx_buffer[1], rx_buffer[2]);

                if (decode_jedec_id(rx_buffer[0], rx_buffer[1], rx_buffer[2])) {
                    print_jedec_report(rx_buffer[0], rx_buffer[1], rx_buffer[2]);
                } else {
                    printf("ERROR: Invalid JEDEC response\n");
                }
            } else {
                printf("ERROR: JEDEC read failed\n");
            }

            print_separator();
            get_menu_choice();
            break;
        }
        case '2': {
            clear_screen();
            print_header("EXECUTING SAFE READ SEQUENCE");

            size_t report_size = get_expected_report_size();
            printf("\nReport size: %zu bytes\n", report_size);
            print_separator();

            uint8_t *master_rx_buffer = malloc(report_size);
            if (!master_rx_buffer) {
                printf("Memory Allocation Failed\n");
                break;
            }

            mutex_enter_blocking(&spi_mutex);
            int stored = spi_OPSAFE_transfer(SPI_PORT, master_rx_buffer, report_size);
            mutex_exit(&spi_mutex);

            if (stored > 0) {
                print_report_buffer_formatted(master_rx_buffer, stored);
            } else {
                printf("\nSafe Block Transfer FAILED\n");
            }
            free(master_rx_buffer);

            printf("\nPress any key to return...");
            get_menu_choice();
            break;
        }
        case '3': {
            clear_screen();
            print_header("INDIVIDUAL SAFE COMMANDS");

            size_t count = get_safe_command_count();
            printf("\n%zu commands available:\n\n", count);

            for (size_t i = 0; i < count; i++) {
                const opcode *cmd = get_command_by_index(i);
                printf("[%zu] %s (0x%02X)  TX:%zu RX:%zu\n", i, cmd->description,
                       cmd->opcode, cmd->tx_len, cmd->rx_data_len);
            }

            print_separator();
            printf("Choose index: ");
            char idx = get_menu_choice();
            size_t cmd_index = idx - '0';

            const opcode *cmd = get_command_by_index(cmd_index);
            if (!cmd)
                break;

            size_t tx_len = cmd->tx_len;
            size_t rx_len = cmd->rx_data_len;

            uint8_t *txb = malloc(tx_len);
            uint8_t *rxb = malloc(rx_len);
            memset(txb, 0, tx_len);
            memset(rxb, 0, rx_len);

            mutex_enter_blocking(&spi_mutex);
            int res = spi_ONE_transfer(SPI_PORT, *cmd, txb, rxb);
            gpio_put(CS_PIN, 1);
            mutex_exit(&spi_mutex);

            if (res != (int)rx_len) {
                printf("ERROR: SPI returned %d bytes (expected %d)\n", res, (int)rx_len);
            } else {
                print_individual_command(cmd->description, cmd->opcode, rxb, rx_len, 0, rx_len);
            }
            free(txb);
            free(rxb);

            printf("\nPress any key...");
            get_menu_choice();
            break;
        }
        case '4': {
            clear_screen();
            print_header("JSON EXPORT");

            size_t expected = get_expected_report_size();
            uint8_t *report = malloc(expected);

            mutex_enter_blocking(&spi_mutex);
            int stored2 = spi_OPSAFE_transfer(SPI_PORT, report, expected);
            mutex_exit(&spi_mutex);

            size_t json_cap = stored2 * 12 + 4096;
            char *json = malloc(json_cap);

            json_export_full_report(json, json_cap, report, stored2);
            printf("%s\n", json);

            free(json);
            free(report);

            print_separator();
            printf("\nPress any key...");
            get_menu_choice();
            break;
        }
        case '5': {
            clear_screen();
            print_header("READ FLASH");
            uint32_t addr = get_hex_input("Enter Start Address (e.g. 0x0000): ");
            uint32_t len = get_hex_input("Enter Length (bytes): ");

            if (len > 4096) {
                printf("Limiting length to 4096 bytes for CLI display.\n");
                len = 4096;
            }

            uint8_t *buf = malloc(len);
            if (!buf) {
                printf("Memory allocation failed.\n");
                break;
            }

            bool success = flash_read_bytes(addr, buf, len);

            if (success) {
                printf("\nReading %u bytes from 0x%06X:\n", (unsigned int)len, (unsigned int)addr);
                print_separator();
                for (uint32_t i = 0; i < len; i += 16) {
                    printf("0x%06X: ", (unsigned int)(addr + i));
                    for (uint32_t j = 0; j < 16; j++) {
                        if (i + j < len) printf("%02X ", buf[i + j]);
                        else printf("   ");
                    }
                    printf("| ");
                    for (uint32_t j = 0; j < 16; j++) {
                        if (i + j < len) {
                            uint8_t c = buf[i + j];
                            printf("%c", (c >= 32 && c <= 126) ? c : '.');
                        }
                    }
                    printf("\n");
                }
                print_separator();
            } else {
                printf("\n✗ Read Failed (SPI Error)\n");
            }
            free(buf);
            printf("\nPress any key...");
            get_menu_choice();
            break;
        }
        case '6': {
            clear_screen();
            print_header("WRITE FLASH (Text Mode)");
            uint32_t addr = get_hex_input("Enter Start Address (e.g. 0x0000): ");
            printf("Enter string to write: ");
            char text_buf[256];
            get_input_line(text_buf, 256);
            size_t len = strlen(text_buf);

            if (confirm_destructive("Writing data will overwrite existing content.")) {
                if (flash_program_data(addr, (uint8_t *)text_buf, len)) {
                    printf("\n✓ Successfully wrote %zu bytes to 0x%06X\n", len, (unsigned int)addr);
                } else {
                    printf("\n✗ Write Failed.\n");
                }
            } else {
                printf("\nOperation cancelled.\n");
            }
            printf("\nPress any key...");
            get_menu_choice();
            break;
        }
        case '7': {
            clear_screen();
            print_header("ERASE SECTOR");
            uint32_t addr = get_hex_input("Enter Address in Sector (e.g. 0x1000): ");
            uint32_t sector_start = addr & ~(4096 - 1); // Manual 4096 alignment

            char msg[64];
            snprintf(msg, 64, "Erasing 4KB sector at 0x%06X", (unsigned int)sector_start);

            if (confirm_destructive(msg)) {
                printf("Erasing...");
                if (flash_erase_sector(sector_start)) {
                    printf("\n✓ Sector Erased Successfully\n");
                } else {
                    printf("\n✗ Erase Failed.\n");
                }
            } else {
                printf("\nOperation cancelled.\n");
            }
            printf("\nPress any key...");
            get_menu_choice();
            break;
        }
        case '8': {
            clear_screen();
            print_header("OPCODE FUZZER");
            printf("\nWARNING: This scans all 256 opcodes (0x00-0xFF).\n");
            printf("This may trigger undocumented Erase or Lock commands.\n");
            printf("If the chip hangs, power cycle the device.\n");

            if (confirm_destructive("Start Blind Opcode Scan?")) {
                mutex_enter_blocking(&spi_mutex);
                spi_fuzz_scan(SPI_PORT);
                mutex_exit(&spi_mutex);
            } else {
                printf("\nScan cancelled.\n");
            }
            printf("\nPress any key...");
            get_menu_choice();
            break;
        }
        default: {
            printf("\nInvalid choice.\n");
            sleep_ms(1000);
        }
        }
    }
}