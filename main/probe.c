#include "probe.h"
#include "board.h"

#include <string.h>
#include "driver/gpio.h"

#if defined(BUSWARE_EUL)
static const uint32_t s_bauds[] = { BW_BAUD, BW_BAUD_ALT };
#else
static const uint32_t s_bauds[] = { BW_BAUD };
#endif
static int s_baud_idx = 0;

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
}

uint32_t bw_probe_baud(void) { return s_bauds[s_baud_idx]; }

void bw_probe_next_baud(void)
{
    int n = sizeof(s_bauds) / sizeof(s_bauds[0]);
    if (n < 2) return;
    s_baud_idx = (s_baud_idx + 1) % n;
    uart_open(s_bauds[s_baud_idx]);
}

void bw_probe_init(void)
{
#if defined(BUSWARE_EUL)
    // SET/NRST auf inaktiv -- das Modul soll im Normalmodus laufen.  Das
    // vollstaendige Boot-Ritual steht in EULFW32 src/hal/tcm515.{h,cpp} und
    // wird hier bewusst nicht nachgebaut: im Normalbetrieb laeuft das Modul,
    // es wird nur gefragt.
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
#endif
    uart_open(s_bauds[s_baud_idx]);
}

#if defined(BUSWARE_TUL)

// U_Reset.req -> U_Reset.ind, Datenblatt Fig. 31, S. 34.  Der einzige Dialog
// ohne Vorbedingungen: er fuehrt den Baustein in einen definierten Zustand und
// quittiert genau dann, wenn die Strecke steht.
#define U_RESET_REQ 0x01
#define U_RESET_IND 0x03

void bw_probe_send(void)
{
    uint8_t req = U_RESET_REQ;
    uart_write_bytes(BW_UART_PORT, &req, 1);
}

bool bw_probe_match(const uint8_t *buf, int len)
{
    for (int i = 0; i < len; i++)
        if (buf[i] == U_RESET_IND) return true;
    return false;
}

#else  // BUSWARE_EUL

// ESP3 CO_RD_VERSION (EnOcean Serial Protocol 3).  Fuer den Probe genuegt ein
// Antwortpaket mit gueltigem Sync-Byte; der Inhalt wird nicht ausgewertet.
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

void bw_probe_send(void)
{
    uint8_t hdr[4] = { 0x00, 0x01, 0x00, 0x05 };  // DataLen=1, OptLen=0, COMMON_COMMAND
    uint8_t data   = 0x03;                        // CO_RD_VERSION
    uint8_t frame[8];
    frame[0] = 0x55;
    memcpy(frame + 1, hdr, 4);
    frame[5] = crc8(hdr, 4);
    frame[6] = data;
    frame[7] = crc8(&data, 1);
    uart_write_bytes(BW_UART_PORT, frame, sizeof(frame));
}

bool bw_probe_match(const uint8_t *buf, int len)
{
    for (int i = 0; i < len; i++)
        if (buf[i] == 0x55) return true;
    return false;
}
#endif
