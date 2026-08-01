// Pins und Transceiver-Parameter je Produkt und Chip.
//
// Jede Zahl ist belegt, keine ist geraten:
//
//   TUL32 (C6)  IO4/IO5, 38400 8E1
//               - TESTING/selftest-fw/boards/tul32.h (Schaltplan TUL32.sch,
//                 auf Hardware gegengeprueft 2026-07-29)
//               - unabhaengig bestaetigt durch den IDF-Prototyp
//                 projects/serial_bridge/main/serial_bridge.c (TXD 4, RXD 5,
//                 38400, UART_PARITY_EVEN)
//               - on-air bestaetigt 2026-08-01: U_Reset.req -> 0x03,
//                 U_State.req -> 0x07, U_SystemStat.req -> 0x4B 0xFB
//   EUL32 (C6)  IO4/IO5, SET=IO2, RST=IO3
//               - TESTING/selftest-fw/boards/eul32.h; die UART-Richtung ist
//                 2026-07-30 auf Hardware entschieden worden (CO_RD_VERSION
//                 beantwortet, desc="TCM515", 460800 Baud)
//   TUL/EUL(C3) TX=21, RX=20
//               - busware-esp32/variants/busware32c3/pins_arduino.h, so laeuft
//                 die Legacy-Firmware im Feld
#pragma once

#include "driver/uart.h"

#if defined(BUSWARE_TUL)
  #define BW_PRODUCT      "TUL"
  #define BW_TRANSCEIVER  "NCN5130"
#elif defined(BUSWARE_EUL)
  #define BW_PRODUCT      "EUL"
  #define BW_TRANSCEIVER  "TCM515"
#else
  #error "Kein Produkt gesetzt -- build.sh uebergibt -DBW_PRODUCT=TUL|EUL"
#endif

// Der Transceiver haengt immer an UART1; UART0 bleibt unangetastet, damit
// weder ROM- noch Bootloader-Ausgaben auf den Transceiver-Pins landen.
#define BW_UART_PORT   UART_NUM_1

#if defined(CONFIG_IDF_TARGET_ESP32C6)
  #define BW_TX        4
  #define BW_RX        5
  #define BW_LED       8
  #define BW_LED_ACTIVE_LOW 1     // LED1 blau, Kathode an IO8 ueber R2=820R
  #if defined(BUSWARE_EUL)
    #define BW_TCM_SET 2          // T1 Pin 31, Programmier-/Bootmodus
    #define BW_TCM_RST 3          // T1 NRST
  #endif

#elif defined(CONFIG_IDF_TARGET_ESP32C3)
  #define BW_TX        21
  #define BW_RX        20
  #define BW_LED       4
  #define BW_LED_ACTIVE_LOW 0
  #if defined(BUSWARE_EUL)
    #define BW_TCM_SET 5          // turboPIN der alten TCMTransceiver-Klasse
    #define BW_TCM_RST 3          // resetPIN  der alten TCMTransceiver-Klasse
  #endif

#else
  #error "Nicht unterstuetzter Chip -- gebaut wird fuer esp32c3 und esp32c6"
#endif

#if defined(BUSWARE_TUL)
  // NCN5130 am KNX-TP.  8E1 ist keine Wahl, sondern Vorgabe des Bausteins.
  #define BW_BAUD       38400
  #define BW_PARITY     UART_PARITY_EVEN
#else
  // TCM515 laeuft mit gezogenem SET-Pin auf 460800.  Klone koennen nur
  // low speed -- deshalb probiert der Probe beide, statt eine zu erzwingen.
  #define BW_BAUD       460800
  #define BW_BAUD_ALT   57600
  #define BW_PARITY     UART_PARITY_DISABLE
#endif
