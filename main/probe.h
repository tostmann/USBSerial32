// Transceiver-Probe: stellt fest, ob der Baustein antwortet -- und zwar
// solange, bis er es tut.
//
// ZWEI GRUENDE FUER DIE WIEDERHOLUNG:
//
// 1. Der Baustein braucht nach dem Boot laenger als die Firmware.  Der NCN5130
//    verwirft Anfragen in den Zustaenden RESET ("services are ignored and not
//    executed") und POWER-UP, solange VBUS/VFILT/XTAL nicht stabil sind
//    (Datenblatt Tab. 10, S. 29).  Gemessen: ein fabrikfrischer TUL-C3
//    brauchte zwei Versuche ueber 749 ms, ein eingelaufener TUL32-C6 einen
//    ueber 299 ms.  Die Altfirmware fragte genau einmal ~90 ms nach Boot und
//    schrieb sonst FAIL -- bei intakter Hardware.
//
// 2. DIE BUSSPANNUNG KANN SPAETER KOMMEN.  Der NCN5130 wird aus dem KNX-Bus
//    gespeist, nicht aus USB; ohne Bus ist er im Reset State und laut
//    Datenblatt (S. 20) ist "communication between host and NCN5130" dann
//    ueberhaupt nicht moeglich.  Ein Stick, der am USB haengt und dessen Bus
//    erst danach zugeschaltet wird, muss sich selbst faengt -- ein einmal
//    gefaelltes FAIL waere fuer den Rest der Laufzeit schlicht falsch.
//
// UND DIE GRENZE: Sobald der Host selbst spricht, hoert die Wiederholung
// SOFORT und endgueltig auf.  knxd fuehrt den Baustein mit eigenem U_Reset.req
// hoch und pollt danach alle 10 s mit U_State.req; ein ungefragtes 0x01 aus
// der Firmware wuerde ihm den NCN mitten im Betrieb zuruecksetzen.  Ab dem
// ersten Host-Byte ist die Bruecke ausschliesslich Bruecke.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// UART aufsetzen (Pins, Baudrate, Parity).  Muss vor allem anderen laufen.
void bw_probe_init(void);

// Eine Anfrage stellen.  Die Antwort wertet bw_probe_match() aus, weil sie im
// Bruecken-Task ankommt -- nur dort wird die UART gelesen.
void bw_probe_send(void);

// Sind in diesem Block die erwarteten Antwortbytes? (NCN: U_Reset.ind 0x03,
// TCM: ESP3-Sync 0x55)
bool bw_probe_match(const uint8_t *buf, int len);

// Baudrate, mit der zuletzt gefragt wurde.  Beim EUL wechselt der Probe
// zwischen 460800 und 57600, weil TCM-Klone nur low speed koennen.
uint32_t bw_probe_baud(void);

// Naechste Baudrate einstellen (nur EUL; beim TUL ohne Wirkung).
void bw_probe_next_baud(void);
