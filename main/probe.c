#include "probe.h"
#include "board.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "driver/gpio.h"

// Fenster, in dem der Transceiver antworten muss.  Grosszuegig gewaehlt: der
// NCN5130 laeuft nach Power-On erst ueber RESET und POWER-UP in den Normal
// State, und wie lange das dauert haengt am Aufladen des VFILT-Tanks, nicht an
// uns.  Die ip4knx-Firmware wartet auf derselben Hardware pauschal 1500 ms;
// hier wird stattdessen gepollt, damit ein wacher Transceiver sofort erkannt
// wird und ein langsamer trotzdem eine Chance bekommt.
#define BW_PROBE_WINDOW_MS   3000
#define BW_PROBE_GAP_MS       150
#define BW_REPLY_WAIT_MS      300

static void uart_open(uint32_t baud)
{
    uart_config_t cfg = {
        .baud_rate  = (int)baud,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = BW_PARITY,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_param_config(BW_UART_PORT, &cfg);
    uart_flush_input(BW_UART_PORT);
}

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

#if defined(BUSWARE_TUL)

// U_Reset.req -> U_Reset.ind, Datenblatt Fig. 31, S. 34.  Das ist der einzige
// Dialog, der ohne Vorbedingungen funktioniert: er fuehrt den Baustein in den
// definierten Zustand und quittiert genau dann, wenn die Strecke steht.
#define U_RESET_REQ  0x01
#define U_RESET_IND  0x03

static bool probe_once(bw_probe_result_t *out, uint32_t baud)
{
    uint8_t req = U_RESET_REQ;
    uart_flush_input(BW_UART_PORT);
    uart_write_bytes(BW_UART_PORT, &req, 1);

    uint8_t buf[8];
    int len = uart_read_bytes(BW_UART_PORT, buf, sizeof(buf),
                              pdMS_TO_TICKS(BW_REPLY_WAIT_MS));
    for (int i = 0; i < len; i++) {
        if (buf[i] == U_RESET_IND) {
            out->baud = baud;
            out->reply_len = (uint8_t)(len > 8 ? 8 : len);
            memcpy(out->reply, buf, out->reply_len);
            return true;
        }
    }
    return false;
}

#else  // BUSWARE_EUL

// ESP3 CO_RD_VERSION (EnOcean Serial Protocol 3).  Der TCM515 antwortet mit
// einem RESPONSE-Paket; fuer den Probe genuegt ein wohlgeformter Sync mit
// gueltiger Header-CRC, der Inhalt wird nicht ausgewertet.
//
// Das Boot-Ritual des Moduls (SET/NRST-Sequenz, Programmier- vs Normalbaud)
// steht ausformuliert in EULFW32 src/hal/tcm515.{h,cpp} und wird hier BEWUSST
// nicht nachgebaut: im Normalbetrieb laeuft das Modul bereits, es wird nur
// gefragt.  SET/RST werden lediglich auf ihren inaktiven Pegel gelegt.
static uint8_t crc8(const uint8_t *d, int len)
{
    uint8_t crc = 0;
    for (int i = 0; i < len; i++) {
        crc ^= d[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    }
    return crc;
}

static bool probe_once(bw_probe_result_t *out, uint32_t baud)
{
    // Header: DataLen=0x0001, OptLen=0x00, PacketType=0x05 (COMMON_COMMAND)
    uint8_t hdr[4] = { 0x00, 0x01, 0x00, 0x05 };
    uint8_t data   = 0x03;                       // CO_RD_VERSION
    uint8_t frame[8];
    frame[0] = 0x55;
    memcpy(frame + 1, hdr, 4);
    frame[5] = crc8(hdr, 4);
    frame[6] = data;
    frame[7] = crc8(&data, 1);

    uart_flush_input(BW_UART_PORT);
    uart_write_bytes(BW_UART_PORT, frame, sizeof(frame));

    uint8_t buf[8];
    int len = uart_read_bytes(BW_UART_PORT, buf, sizeof(buf),
                              pdMS_TO_TICKS(BW_REPLY_WAIT_MS));
    for (int i = 0; i < len; i++) {
        if (buf[i] == 0x55) {
            out->baud = baud;
            out->reply_len = (uint8_t)(len > 8 ? 8 : len);
            memcpy(out->reply, buf, out->reply_len);
            return true;
        }
    }
    return false;
}
#endif

void bw_probe(bw_probe_result_t *out)
{
    memset(out, 0, sizeof(*out));

#if defined(BUSWARE_EUL)
    // SET/NRST auf inaktiv -- das Modul soll im Normalmodus laufen.
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << BW_TCM_SET) | (1ULL << BW_TCM_RST),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(BW_TCM_SET, 1);
    gpio_set_level(BW_TCM_RST, 1);

    const uint32_t bauds[] = { BW_BAUD, BW_BAUD_ALT };
#else
    const uint32_t bauds[] = { BW_BAUD };
#endif
    const int nbauds = sizeof(bauds) / sizeof(bauds[0]);

    uint32_t t0 = now_ms();
    out->baud = BW_BAUD;

    // Rundlauf ueber die Baudraten, bis das Fenster zu ist.  Ein Transceiver,
    // der schon wach ist, meldet sich im ersten Versuch -- der Rest ist
    // Nachsicht gegenueber einem, der noch hochlaeuft.
    while ((now_ms() - t0) < BW_PROBE_WINDOW_MS) {
        for (int i = 0; i < nbauds; i++) {
            uart_open(bauds[i]);
            out->attempts++;
            if (probe_once(out, bauds[i])) {
                out->ok = true;
                out->elapsed_ms = now_ms() - t0;
                uart_open(out->baud);     // mit der Baudrate weiterarbeiten,
                return;                   // die tatsaechlich geantwortet hat
            }
        }
        vTaskDelay(pdMS_TO_TICKS(BW_PROBE_GAP_MS));
    }

    out->elapsed_ms = now_ms() - t0;
    uart_open(out->baud);                 // Bruecke laeuft auch ohne Quittung
}
