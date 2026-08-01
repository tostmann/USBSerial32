// USBSerial32 -- byte-transparente Bruecke USB-Serial-JTAG <-> Transceiver-UART
// fuer die busware-Sticks TUL und EUL auf ESP32-C3 und ESP32-C6.
//
// Ersetzt die alte Arduino-Firmware busware-*-serial-transparent, deren Quelle
// nicht mehr auffindbar ist.  Was hier bewusst anders ist:
//
//   1. Der Transceiver-Test heilt sich selbst (siehe probe.h).  Er fragt
//      wiederholt, weil der Baustein spaeter bereit sein kann als die Firmware
//      -- und weil die Busspannung, aus der er lebt, erst nach dem Einstecken
//      zugeschaltet werden kann.  Die Altfirmware fragte einmal, ~90 ms nach
//      Boot, und blieb dann bei ihrem Urteil.
//   2. Er hoert auf, sobald der Host spricht.  knxd faehrt den NCN mit eigenem
//      U_Reset.req hoch und pollt alle 10 s mit U_State.req; ein ungefragtes
//      0x01 aus der Firmware wuerde ihm den Baustein im Betrieb zuruecksetzen.
//   3. Der Nutzdatenstrom bleibt sauber: Konsole und Bootloader-Log sind aus,
//      Ausgaben gibt es nur als Banner (und mit -DBW_QUIET=1 gar nicht).
//   4. Zwei Tasks statt Polling-Schleife -- jede Richtung blockiert auf ihrer
//      Quelle, das haelt die Latenz unten.
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/usb_serial_jtag.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#include "board.h"
#include "probe.h"

#ifndef BW_VERSION
#define BW_VERSION "0.0.0"
#endif

#define BW_BUFSIZE        1024
#define BW_UART_BUFSIZE   (BW_BUFSIZE * 2)

// Anfangs schnell fragen (der Baustein laeuft meist nur Millisekunden hinterher),
// danach geduldig -- da wartet man auf eine Busspannung, die Minuten spaeter
// kommen kann.
#define BW_FAST_TRIES     10
#define BW_FAST_GAP_MS    300
#define BW_SLOW_GAP_MS    2000
#define BW_VERDICT_MS     3000   // wann der "noch nichts"-Banner faellt

static atomic_bool s_tx_ok     = false;   // Transceiver hat geantwortet
static atomic_bool s_host_seen = false;   // Host hat gesprochen -> Probe aus
static atomic_uint s_traffic   = 0;
static uint32_t    s_attempts  = 0;
static uint32_t    s_t0        = 0;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

// --- Banner ----------------------------------------------------------------
// Geht absichtlich NICHT ueber printf: die Konsole ist abgeschaltet, damit
// keine Log-Zeile je im Nutzdatenstrom landet.
static void say(const char *msg)
{
#if BW_QUIET
    (void)msg;
#else
    usb_serial_jtag_write_bytes((const uint8_t *)msg, strlen(msg), pdMS_TO_TICKS(200));
#endif
}

static void banner_ok(void)
{
    char line[192];
    snprintf(line, sizeof(line),
             "busware %s serial bridge %s (%s) -- %s OK @%lu baud,"
             " %lu ms, %lu Versuch%s\r\n",
             BW_PRODUCT, BW_VERSION, CONFIG_IDF_TARGET, BW_TRANSCEIVER,
             (unsigned long)bw_probe_baud(), (unsigned long)(now_ms() - s_t0),
             (unsigned long)s_attempts, s_attempts == 1 ? "" : "e");
    say(line);
}

static void banner_waiting(void)
{
    char line[224];
    snprintf(line, sizeof(line),
             "busware %s serial bridge %s (%s) -- %s meldet sich nicht"
             " (Busspannung?); Bruecke laeuft, Test laeuft weiter\r\n",
             BW_PRODUCT, BW_VERSION, CONFIG_IDF_TARGET, BW_TRANSCEIVER);
    say(line);
}

// --- LED -------------------------------------------------------------------
static void led_set(int on)
{
#if BW_LED_ACTIVE_LOW
    gpio_set_level(BW_LED, on ? 0 : 1);
#else
    gpio_set_level(BW_LED, on ? 1 : 0);
#endif
}

// Optische Diagnose ohne Terminal: hektisches Blinken = Transceiver antwortet
// nicht (typisch: keine Busspannung).  Ruhiger Herzschlag = alles gut.
// Aufblitzen = Verkehr.
static void led_task(void *arg)
{
    unsigned seen = 0;
    int state = 0;
    while (1) {
        unsigned t = atomic_load(&s_traffic);
        if (t != seen) {
            seen = t;
            led_set(1); vTaskDelay(pdMS_TO_TICKS(20));
            led_set(0); vTaskDelay(pdMS_TO_TICKS(30));
        } else if (!atomic_load(&s_tx_ok) && !atomic_load(&s_host_seen)) {
            state = !state;
            led_set(state);
            vTaskDelay(pdMS_TO_TICKS(120));
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
        if (n <= 0) continue;

        // Solange der Test laeuft, kommt die Antwort hier an -- nur dieser Task
        // liest die UART.  Weitergereicht wird sie trotzdem: verschlucken waere
        // eine Luege gegenueber einem Host, der schon zuhoert.
        if (!atomic_load(&s_tx_ok) && !atomic_load(&s_host_seen)
            && bw_probe_match(buf, n)) {
            atomic_store(&s_tx_ok, true);
            banner_ok();
        }

        usb_serial_jtag_write_bytes(buf, n, portMAX_DELAY);
        atomic_fetch_add(&s_traffic, 1);
    }
}

static void usb_to_uart(void *arg)
{
    uint8_t *buf = malloc(BW_BUFSIZE);
    if (!buf) vTaskDelete(NULL);
    while (1) {
        int n = usb_serial_jtag_read_bytes(buf, BW_BUFSIZE, pdMS_TO_TICKS(20));
        if (n <= 0) continue;
        // Ab jetzt gehoert der Baustein dem Host.  Kein eigener Verkehr mehr.
        atomic_store(&s_host_seen, true);
        uart_write_bytes(BW_UART_PORT, (const char *)buf, n);
        atomic_fetch_add(&s_traffic, 1);
    }
}

// --- Selbstheilender Test --------------------------------------------------
static void probe_task(void *arg)
{
    bool said_waiting = false;

    while (!atomic_load(&s_tx_ok) && !atomic_load(&s_host_seen)) {
        bw_probe_send();
        s_attempts++;

        uint32_t gap = (s_attempts <= BW_FAST_TRIES) ? BW_FAST_GAP_MS : BW_SLOW_GAP_MS;
        vTaskDelay(pdMS_TO_TICKS(gap));

        if (!said_waiting && !atomic_load(&s_tx_ok)
            && (now_ms() - s_t0) >= BW_VERDICT_MS) {
            banner_waiting();
            said_waiting = true;
        }
        // Beim EUL zwischen 460800 und 57600 wechseln: TCM-Klone koennen nur
        // low speed, und welcher verbaut ist, sagt uns nur die Antwort.
        if (!atomic_load(&s_tx_ok)) bw_probe_next_baud();
    }
    vTaskDelete(NULL);
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
    bw_probe_init();

    usb_serial_jtag_driver_config_t usb = {
        .rx_buffer_size = BW_UART_BUFSIZE,
        .tx_buffer_size = BW_UART_BUFSIZE,
    };
    usb_serial_jtag_driver_install(&usb);

    s_t0 = now_ms();

    // Bruecke zuerst -- sie laeuft unabhaengig davon, ob der Transceiver sich
    // meldet.  Ein Stick, der wegen fehlender Busspannung nicht antwortet, ist
    // trotzdem ein funktionierender Adapter.
    xTaskCreate(uart_to_usb, "uart2usb", 3072, NULL, 12, NULL);
    xTaskCreate(usb_to_uart, "usb2uart", 3072, NULL, 12, NULL);
    xTaskCreate(probe_task,  "probe",    3072, NULL,  5, NULL);
    xTaskCreate(led_task,    "led",      2048, NULL,  3, NULL);
}
