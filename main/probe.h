// Transceiver-Probe: stellt beim Start fest, ob der Baustein antwortet.
//
// WARUM DAS HIER MIT WIEDERHOLUNG ARBEITET (und die Altfirmware nicht):
// Der NCN5130 kennt laut Datenblatt (Tabelle 10, S. 29) Zustaende, in denen er
// Anfragen schlicht verwirft -- RESET ("services are ignored and not executed")
// und POWER-UP, solange VBUS/VFILT/XTAL nicht stabil sind.  Ein einmaliger
// Versuch direkt nach dem Boot kann darum fehlschlagen, obwohl Hardware und
// Verkabelung in Ordnung sind; genau dieses Bild -- Banner meldet FAIL, ein
// spaeterer Dialog laeuft einwandfrei -- hat die alte Bridge am 2026-08-01
// reproduzierbar am Prueflatz erzeugt.  Ein Selbsttest, der bei intaktem Geraet
// Alarm schlaegt, ist schlimmer als keiner: er erzeugt Supportfaelle.
//
// Deshalb: mehrere Versuche ueber ein Zeitfenster, und erst danach ein Urteil.
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool     ok;            // Transceiver hat gueltig geantwortet
    uint32_t baud;          // Baudrate, mit der er geantwortet hat
    uint32_t attempts;      // wieviele Versuche noetig waren
    uint32_t elapsed_ms;    // wie lange es gedauert hat
    uint8_t  reply[8];      // erste Antwortbytes, fuer den Banner
    uint8_t  reply_len;
} bw_probe_result_t;

// Fuehrt den produktspezifischen Probe aus.  Laesst die UART danach mit der
// Baudrate offen, die geantwortet hat (bzw. mit der Vorgabe, wenn keine).
void bw_probe(bw_probe_result_t *out);
