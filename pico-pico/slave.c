
// slave.c — software SPI "slave" (no PIO, no hardware SPI)
// Echoes SLAVE_TX_BYTE on MISO while capturing one byte from MOSI per CSn
// window.

#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// Pins (same physical wiring as your master uses)
#define SCK_PIN 2  // SPI SCK from master
#define MOSI_PIN 3 // Master -> Slave
#define MISO_PIN 4 // Slave  -> Master
#define CS_PIN 5   // Active-low from master

#define SLAVE_TX_BYTE 0xB2
#define TIMEOUT_US 200000 // 200 ms edge wait timeout (avoid deadlocks)

// Wait for a pin to become level (0/1) with a timeout; returns false on
// timeout.
static bool wait_for_level(uint pin, bool level) {
  const uint64_t start = time_us_64();
  while (gpio_get(pin) != level) {
    if (time_us_64() - start > TIMEOUT_US)
      return false;
    tight_loop_contents();
  }
  return true;
}

int main() {
  stdio_init_all();
  sleep_ms(500);
  printf("--- Software SPI Slave (Mode 0) ---\n");

  // Configure pins
  gpio_init(SCK_PIN);
  gpio_set_dir(SCK_PIN, GPIO_IN);
  gpio_init(MOSI_PIN);
  gpio_set_dir(MOSI_PIN, GPIO_IN);
  gpio_init(MISO_PIN);
  gpio_set_dir(MISO_PIN, GPIO_OUT);
  gpio_put(MISO_PIN, 0);
  gpio_init(CS_PIN);
  gpio_set_dir(CS_PIN, GPIO_IN);
  gpio_pull_up(CS_PIN);

  while (true) {
    // Idle until CSn asserted (goes LOW)
    if (gpio_get(CS_PIN)) {
      tight_loop_contents();
      continue;
    }

    // One transaction per CSn pulse: capture 8 bits, drive 8 bits
    uint8_t tx = SLAVE_TX_BYTE;
    uint8_t rx = 0;
    int bit = 7;

    // MODE 0 timing: sample on SCK rising edge, change data on falling edge.
    // Present the first MISO bit *before* the first rising edge:
    gpio_put(MISO_PIN, (tx >> 7) & 1);

    while (!gpio_get(CS_PIN)) {
      // Receive 8 bits while CSn is low
      // Rising edge: sample MOSI into current bit
      if (!wait_for_level(SCK_PIN, true))
        break;
      if (gpio_get(MOSI_PIN))
        rx |= (1u << bit);

      // Falling edge: shift to next bit and update MISO
      if (!wait_for_level(SCK_PIN, false))
        break;
      if (--bit < 0)
        break;
      gpio_put(MISO_PIN, (tx >> bit) & 1);
    }

    // If CSn deasserted early, we still print what we got so far
    printf("[Slave] RECEIVED: 0x%02X, RESPONDED: 0x%02X\n", rx, SLAVE_TX_BYTE);

    // Deassert MISO to a safe idle (optional)
    gpio_put(MISO_PIN, 0);

    // Wait for CSn to return high (ensure clean re-arm)
    while (!gpio_get(CS_PIN))
      tight_loop_contents();
  }
  return 0;
}
