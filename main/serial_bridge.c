// USBSerial32 -- byte-transparente Bruecke USB-Serial-JTAG <-> Transceiver-UART
// fuer die busware-Sticks TUL und EUL auf ESP32-C3 und ESP32-C6.
//
// Ersetzt die alte Arduino-Firmware busware-*-serial-transparent, deren Quelle
// nicht mehr auffindbar ist.  Drei Dinge sind gegenueber ihr bewusst anders:
//
//   1. Der Transceiver-Probe wiederholt (siehe probe.h).  Die Altfirmware
//      fragte genau einmal, rund 90 ms nach dem Boot, und schrieb bei
//      Ausbleiben der Antwort FAIL -- auch dann, wenn der Baustein Millisekunden
//      spaeter einwandfrei arbeitete.
//   2. Der Nutzdatenstrom bleibt sauber.  Konsole und Bootloader-Log sind
//      abgeschaltet, der Banner wird bewusst und einmalig geschrieben (und mit
//      -DBW_QUIET=1 gar nicht).  Die Altfirmware liess Bootloader-Meldungen
//      wie "Factory app partition is not bootable" in den Datenstrom laufen,
//      vor dem ersten Nutzbyte.
//   3. Zwei Tasks statt Polling-Schleife -- jede Richtung blockiert auf ihrer
//      Quelle, das haelt die Latenz unten.
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/usb_serial_jtag.h"
#include "driver/gpio.h"

#include "board.h"
#include "probe.h"

#ifndef BW_VERSION
#define BW_VERSION "0.0.0"
#endif

#define BW_BUFSIZE      1024
#define BW_UART_BUFSIZE (BW_BUFSIZE * 2)

static volatile uint32_t s_traffic = 0;

// --- LED -------------------------------------------------------------------
static void led_set(int on)
{
#if BW_LED_ACTIVE_LOW
    gpio_set_level(BW_LED, on ? 0 : 1);
#else
    gpio_set_level(BW_LED, on ? 1 : 0);
#endif
}

static void led_task(void *arg)
{
    uint32_t seen = 0;
    int state = 0;
    while (1) {
        // Verkehr -> kurzes Aufblitzen, sonst ruhiger Herzschlag.
        if (s_traffic != seen) {
            seen = s_traffic;
            led_set(1);
            vTaskDelay(pdMS_TO_TICKS(20));
            led_set(0);
            vTaskDelay(pdMS_TO_TICKS(30));
        } else {
            state = !state;
            led_set(state);
            vTaskDelay(pdMS_TO_TICKS(state ? 40 : 1960));
        }
    }
}

// --- Bruecke ---------------------------------------------------------------
static void uart_to_usb(void *arg)
{
    uint8_t *buf = malloc(BW_BUFSIZE);
    if (!buf) vTaskDelete(NULL);
    while (1) {
        int n = uart_read_bytes(BW_UART_PORT, buf, BW_BUFSIZE, pdMS_TO_TICKS(20));
        if (n > 0) {
            usb_serial_jtag_write_bytes(buf, n, portMAX_DELAY);
            s_traffic++;
        }
    }
}

static void usb_to_uart(void *arg)
{
    uint8_t *buf = malloc(BW_BUFSIZE);
    if (!buf) vTaskDelete(NULL);
    while (1) {
        int n = usb_serial_jtag_read_bytes(buf, BW_BUFSIZE, pdMS_TO_TICKS(20));
        if (n > 0) {
            uart_write_bytes(BW_UART_PORT, (const char *)buf, n);
            s_traffic++;
        }
    }
}

// --- Banner ----------------------------------------------------------------
// Geht absichtlich NICHT ueber printf: die Konsole ist abgeschaltet, damit
// keine Log-Zeile je im Nutzdatenstrom landet.  Der Banner ist die einzige
// Ausgabe der Firmware und steht genau einmal, vor dem ersten Nutzbyte.
static void banner(const bw_probe_result_t *p)
{
#if BW_QUIET
    (void)p;
#else
    char line[192];
    int n = snprintf(line, sizeof(line),
                     "busware %s serial bridge %s (%s) -- %s %s",
                     BW_PRODUCT, BW_VERSION, CONFIG_IDF_TARGET,
                     BW_TRANSCEIVER, p->ok ? "OK" : "FAIL");
    if (p->ok)
        n += snprintf(line + n, sizeof(line) - n,
                      " @%lu baud, %lu ms, %lu %s",
                      (unsigned long)p->baud, (unsigned long)p->elapsed_ms,
                      (unsigned long)p->attempts,
                      p->attempts == 1 ? "Versuch" : "Versuche");
    else
        n += snprintf(line + n, sizeof(line) - n,
                      " -- keine Antwort in %lu ms (%lu Versuche);"
                      " Bruecke laeuft trotzdem",
                      (unsigned long)p->elapsed_ms, (unsigned long)p->attempts);
    n += snprintf(line + n, sizeof(line) - n, "\r\n");
    usb_serial_jtag_write_bytes((const uint8_t *)line, n, pdMS_TO_TICKS(200));
#endif
}

void app_main(void)
{
    gpio_config_t led = {
        .pin_bit_mask = (1ULL << BW_LED),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&led);
    led_set(0);

    uart_driver_install(BW_UART_PORT, BW_UART_BUFSIZE, 0, 0, NULL, 0);
    uart_set_pin(BW_UART_PORT, BW_TX, BW_RX,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    usb_serial_jtag_driver_config_t usb = {
        .rx_buffer_size = BW_UART_BUFSIZE,
        .tx_buffer_size = BW_UART_BUFSIZE,
    };
    usb_serial_jtag_driver_install(&usb);

    // Erst fragen, dann melden.  bw_probe() konfiguriert die UART.
    bw_probe_result_t probe;
    bw_probe(&probe);
    banner(&probe);

    xTaskCreate(uart_to_usb, "uart2usb", 3072, NULL, 12, NULL);
    xTaskCreate(usb_to_uart, "usb2uart", 3072, NULL, 12, NULL);
    xTaskCreate(led_task,    "led",      2048, NULL,  3, NULL);
}
