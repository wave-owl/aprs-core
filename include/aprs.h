/*
 * aprs-core -- APRS packet decoder, encoder, APRS-IS passcode and server-filter matcher in C99.
 *
 * Copyright (c) 2026 Gerolf Ziegenhain
 *
 * The decoder follows libfap 1.5 by Tapio Aaltonen OH2GVE (GPL-1.0-or-later or Artistic,
 * https://github.com/wave-owl/libfap, commit 074f4ab), itself the C port of Ham::APRS::FAP:
 * packet type dispatch, uncompressed/compressed/Mic-E positions with position ambiguity,
 * objects, items, messages, telemetry, weather, NMEA. libfap needs PCRE; this core matches by
 * hand and has no dependency besides libm. The APRS-IS passcode and the server-side filter
 * follow aprsc by Matti Aarnio OH2MQK and Heikki Hannikainen OH7LZB (BSD licence,
 * https://github.com/wave-owl/aprsc, commit 5378272: src/passcode.c, src/filter.c). aprslib by
 * Rossen Georgiev (GPL-2.0-or-later, https://github.com/wave-owl/aprs-python, commit c2a0f18)
 * is the independent oracle of the differential vectors; no aprslib code is used. Formats:
 * APRS Protocol Reference 1.0.1 and APRS 1.2 (WB2OSZ).
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by the Free Software Foundation, either version
 * 3 of the License, or (at your option) any later version. See LICENSE and NOTICE.
 */
#ifndef APRS_H
#define APRS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APRS_VERSION_STRING "0.1.0"
#define APRS_ABI 1

/* "aprs-core 0.1.0 contract=aprs-c1 abi=1 enc=1 filter=1". Static string. */
const char *aprs_version(void);
int aprs_abi(void);

#define APRS_SIZEOF_PACKET 0
size_t aprs_sizeof(int what);   /* 0 for an unknown structure */

/* Packet types (aprs_packet.type). */
#define APRS_T_UNKNOWN 0
#define APRS_T_POSITION 1
#define APRS_T_OBJECT 2
#define APRS_T_ITEM 3
#define APRS_T_WAYPOINT 4
#define APRS_T_MESSAGE 5
#define APRS_T_ACK 6
#define APRS_T_REJ 7
#define APRS_T_BULLETIN 8
#define APRS_T_STATUS 9
#define APRS_T_WEATHER 10
#define APRS_T_TELEMETRY 11
#define APRS_T_TELEMETRY_META 12
#define APRS_T_QUERY 13
#define APRS_T_CAPABILITIES 14
#define APRS_T_USER_DEFINED 15

/* Position formats (aprs_packet.format). */
#define APRS_F_NONE 0
#define APRS_F_UNCOMPRESSED 1
#define APRS_F_COMPRESSED 2
#define APRS_F_MICE 3
#define APRS_F_NMEA 4

#define APRS_CALL_LEN 16
#define APRS_MAX_PATH 8
#define APRS_TEXT_LEN 256
#define APRS_INFO_LEN 512

/* One decoded packet. Absent numbers are NaN (doubles) or -1 (ints); absent strings "".
 * Weather in SI units: wind km/h, temperature deg C, rain mm, pressure hPa, humidity %,
 * luminosity W/m^2, snow cm. Speeds km/h, altitude and PHG height m, range km. */
typedef struct {
    char source[APRS_CALL_LEN];
    char dest[APRS_CALL_LEN];
    char path[APRS_MAX_PATH][APRS_CALL_LEN];
    int path_len;
    int type;
    int format;
    int third_party;            /* unwrapped from a '}' third-party packet */
    int messaging;              /* '=' or '@' position */
    int killed;                 /* object/item '_' */
    int has_position;
    double lat, lon;
    int pos_ambiguity;          /* digits blanked, 0..4 */
    char symbol_table;
    char symbol_code;
    int course_deg;             /* 0..359, -1 none */
    double speed_kmh;
    double altitude_m;
    double range_km;
    int phg_power_w;            /* -1 none */
    double phg_height_m;
    int phg_gain_db;
    int phg_dir_deg;            /* 0 omni, 45..360, -1 none */
    char name[APRS_CALL_LEN];   /* object / item / waypoint */
    char comment[APRS_TEXT_LEN];
    char status[APRS_TEXT_LEN];
    char mic_e_message[24];
    int has_weather;
    double wx_wind_dir_deg, wx_wind_speed_kmh, wx_wind_gust_kmh, wx_temp_c, wx_rain_1h_mm,
           wx_rain_24h_mm, wx_rain_midnight_mm, wx_humidity_pct, wx_pressure_hpa,
           wx_luminosity_wm2, wx_snow_24h_cm;
    int has_telemetry;
    char tlm_seq[8];
    double tlm_analog[5];
    char tlm_digital[9];
    char tlm_meta_kind[5];      /* PARM | UNIT | EQNS | BITS */
    char addressee[APRS_CALL_LEN];
    char message_text[APRS_TEXT_LEN];   /* message, bulletin or telemetry-meta values */
    char msgno[8];
    char reply_ack[8];
    char acked_msgno[8];
} aprs_packet;

/* Decode one TNC2 line ("SRC>DEST,PATH:info"). Returns 0 when the header parsed (the type
 * may still be APRS_T_UNKNOWN), -1 when the line is not TNC2. Never reads past the line. */
int aprs_decode(const char *line, aprs_packet *out);

/* APRS-IS passcode of a callsign (SSID ignored), 0..32767; -1 for an empty call. */
int aprs_passcode(const char *callsign);

/* Server-side filter (APRS-IS filter syntax, aprsc semantics): check syntax; 0 ok, else -1
 * with a message in err. Supported: r a p b o t s d e u g m q, '-' exclusions; f (friend
 * range) is accepted but never matches (it needs other stations' positions). */
int aprs_filter_check(const char *spec, char *err, size_t errlen);
/* 1 when the packet passes the filter, 0 when not, -1 for a bad spec. my_lat/my_lon is the
 * station position for m/ (NaN: m/ never matches). An empty spec passes nothing. */
int aprs_filter_match(const char *spec, const aprs_packet *p, double my_lat, double my_lon);

/* Encoders: write a NUL-terminated information field into buf, return its length, or -1
 * for invalid input / -2 when buf is too small. ts_ddhhmm is "DDHHMM" (UTC) or NULL. */
int aprs_encode_position(char *buf, size_t len, double lat, double lon, char table, char code,
                         const char *comment, const char *ts_ddhhmm, int messaging);
int aprs_encode_message(char *buf, size_t len, const char *addressee, const char *text,
                        const char *msgno);
int aprs_encode_ack(char *buf, size_t len, const char *addressee, const char *msgno, int rej);
int aprs_encode_bulletin(char *buf, size_t len, char ident, const char *group,
                         const char *text);
int aprs_encode_status(char *buf, size_t len, const char *text, const char *ts_ddhhmm);
int aprs_encode_object(char *buf, size_t len, const char *name, double lat, double lon,
                       char table, char code, const char *comment, const char *ts_ddhhmm,
                       int kill);
int aprs_encode_item(char *buf, size_t len, const char *name, double lat, double lon,
                     char table, char code, const char *comment, int kill);

#ifdef __cplusplus
}
#endif
#endif
