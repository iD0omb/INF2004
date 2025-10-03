#include "pico/stdlib.h"
#include <stdio.h>

int main() {
    
    //Run once, declarations etc.
    stdio_init_all();
    printf("Hello, Pico!\n");

    const uint LED_PIN = 25;
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    
    //Main loop
    while (true) {
        gpio_put(LED_PIN, 1);
        sleep_ms(500);
        gpio_put(LED_PIN, 0);
        sleep_ms(500);
    }
}
