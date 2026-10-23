/*
 * aprs-core -- information-field encoders (APRS101 ch. 8, 11, 14, 16; APRS 1.2).
 * See include/aprs.h for sources and licence. Every encoder validates its input and writes
 * nothing malformed: invalid input returns -1, a too small buffer -2.
 */
#include "aprs.h"
#include "aprs_internal.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define MAX_MESSAGE_TEXT 67
#define MAX_STATUS_TEXT 62
#define MAX_STATUS_TEXT_TS 55
#define MAX_POSITION_COMMENT 43
#define OBJECT_NAME_LEN 9

static int printable(const char *s, size_t limit, int allow_empty)
{
    size_t n = strlen(s), i;
    if ((!allow_empty && n == 0) || n > limit) return 0;
    for (i = 0; i < n; i++)
        if ((unsigned char)s[i] < 0x20 || (unsigned char)s[i] > 0x7e) return 0;
    return 1;
}

static int message_text_ok(const char *s)
{
    return printable(s, MAX_MESSAGE_TEXT, 0) && !strpbrk(s, "|~{");
}

/* Upper-case callsign, optional SSID 1-15 ("-0" dropped), base 1-6 alphanumerics. */
static int norm_call(const char *in, char *out)
{
    char buf[APRS_CALL_LEN];
    size_t n = strlen(in), i, base;
    const char *dash;
    while (n && in[n - 1] == ' ') n--;
    while (*in == ' ' && n) { in++; n--; }
    if (n == 0 || n >= sizeof(buf)) return 0;
    for (i = 0; i < n; i++) buf[i] = (char)toupper((unsigned char)in[i]);
    buf[n] = '\0';
    if (n > 2 && strcmp(buf + n - 2, "-0") == 0) buf[n -= 2] = '\0';
    dash = strchr(buf, '-');
    base = dash ? (size_t)(dash - buf) : n;
    if (base < 1 || base > 6) return 0;
    for (i = 0; i < base; i++)
        if (!isalnum((unsigned char)buf[i])) return 0;
    if (dash) {
        const char *ssid = dash + 1;
        size_t sl = strlen(ssid);
        if (sl < 1 || sl > 2 || !isdigit((unsigned char)ssid[0]) ||
            (sl == 2 && !isdigit((unsigned char)ssid[1])))
            return 0;
        if (sl == 2 && !(ssid[0] == '1' && ssid[1] <= '5')) return 0;
        if (sl == 2 && ssid[0] == '0') return 0;
    }
    strcpy(out, buf);
    return 1;
}

static int msgno_ok(const char *m)
{
    size_t n = strlen(m), i;
    if (n < 1 || n > 5) return 0;
    for (i = 0; i < n; i++)
        if (!isalnum((unsigned char)m[i])) return 0;
    return 1;
}

static int ts_ok(const char *ts)
{
    size_t i;
    if (!ts) return 1;
    if (strlen(ts) != 6) return 0;
    for (i = 0; i < 6; i++)
        if (!isdigit((unsigned char)ts[i])) return 0;
    return 1;
}

static int emit(char *buf, size_t len, int n)
{
    if (n < 0) return -1;
    if ((size_t)n >= len) return -2;
    (void)buf;
    return n;
}

int aprs_fmt_lat(char *out, double lat)
{
    char mins[16];
    char hemi = lat >= 0 ? 'N' : 'S';
    int deg;
    if (!(lat >= -90.0 && lat <= 90.0)) return 0;
    lat = fabs(lat);
    deg = (int)lat;
    snprintf(mins, sizeof(mins), "%05.2f", (lat - deg) * 60.0);
    if (strcmp(mins, "60.00") >= 0 && strlen(mins) == 5) {
        deg += 1;
        strcpy(mins, "00.00");
    }
    snprintf(out, 9, "%02d%s%c", deg, mins, hemi);
    return 1;
}

int aprs_fmt_lon(char *out, double lon)
{
    char mins[16];
    char hemi = lon >= 0 ? 'E' : 'W';
    int deg;
    if (!(lon >= -180.0 && lon <= 180.0)) return 0;
    lon = fabs(lon);
    deg = (int)lon;
    snprintf(mins, sizeof(mins), "%05.2f", (lon - deg) * 60.0);
    if (strcmp(mins, "60.00") >= 0 && strlen(mins) == 5) {
        deg += 1;
        strcpy(mins, "00.00");
    }
    snprintf(out, 10, "%03d%s%c", deg, mins, hemi);
    return 1;
}

static int symbol_ok(char table, char code)
{
    return table > 0x20 && table < 0x7f && code > 0x20 && code < 0x7f;
}

int aprs_encode_position(char *buf, size_t len, double lat, double lon, char table, char code,
                         const char *comment, const char *ts_ddhhmm, int messaging)
{
    char la[9], lo[10];
    comment = comment ? comment : "";
    if (!symbol_ok(table, code) || !ts_ok(ts_ddhhmm) ||
        !printable(comment, MAX_POSITION_COMMENT, 1) || !aprs_fmt_lat(la, lat) ||
        !aprs_fmt_lon(lo, lon))
        return -1;
    if (ts_ddhhmm)
        return emit(buf, len, snprintf(buf, len, "%c%sz%s%c%s%c%s", messaging ? '@' : '/',
                                       ts_ddhhmm, la, table, lo, code, comment));
    return emit(buf, len, snprintf(buf, len, "%c%s%c%s%c%s", messaging ? '=' : '!', la, table,
                                   lo, code, comment));
}

int aprs_encode_message(char *buf, size_t len, const char *addressee, const char *text,
                        const char *msgno)
{
    char call[APRS_CALL_LEN];
    if (!addressee || !text || !norm_call(addressee, call) || !message_text_ok(text)) return -1;
    if (msgno && !msgno_ok(msgno)) return -1;
    if (msgno)
        return emit(buf, len, snprintf(buf, len, ":%-9s:%s{%s", call, text, msgno));
    return emit(buf, len, snprintf(buf, len, ":%-9s:%s", call, text));
}

int aprs_encode_ack(char *buf, size_t len, const char *addressee, const char *msgno, int rej)
{
    char call[APRS_CALL_LEN];
    if (!addressee || !msgno || !norm_call(addressee, call) || !msgno_ok(msgno)) return -1;
    return emit(buf, len, snprintf(buf, len, ":%-9s:%s%s", call, rej ? "rej" : "ack", msgno));
}

int aprs_encode_bulletin(char *buf, size_t len, char ident, const char *group,
                         const char *text)
{
    char id = (char)toupper((unsigned char)ident);
    char grp[8];
    size_t gl, i;
    char addr[16];
    group = group ? group : "";
    gl = strlen(group);
    if (!(isdigit((unsigned char)id) || (id >= 'A' && id <= 'Z'))) return -1;
    if (gl > 5) return -1;
    for (i = 0; i < gl; i++) {
        if (!isalnum((unsigned char)group[i])) return -1;
        grp[i] = (char)toupper((unsigned char)group[i]);
    }
    grp[gl] = '\0';
    if (gl && !isdigit((unsigned char)id)) return -1;
    if (!text || !message_text_ok(text)) return -1;
    snprintf(addr, sizeof(addr), "BLN%c%s", id, grp);
    return emit(buf, len, snprintf(buf, len, ":%-9s:%s", addr, text));
}

int aprs_encode_status(char *buf, size_t len, const char *text, const char *ts_ddhhmm)
{
    if (!text || !ts_ok(ts_ddhhmm)) return -1;
    if (ts_ddhhmm) {
        if (!printable(text, MAX_STATUS_TEXT_TS, 0)) return -1;
        return emit(buf, len, snprintf(buf, len, ">%sz%s", ts_ddhhmm, text));
    }
    if (!printable(text, MAX_STATUS_TEXT, 0)) return -1;
    return emit(buf, len, snprintf(buf, len, ">%s", text));
}

/* Object/item name: printable, without ! _ | ~, 1 (item: 3) to 9 characters after the
 * trailing blanks are dropped. */
static int object_name(const char *in, char *out, size_t minimum)
{
    size_t n = strlen(in), i;
    if (n > OBJECT_NAME_LEN) return 0;
    for (i = 0; i < n; i++)
        if ((unsigned char)in[i] < 0x20 || (unsigned char)in[i] > 0x7e || strchr("!_|~", in[i]))
            return 0;
    memcpy(out, in, n);
    out[n] = '\0';
    while (n && out[n - 1] == ' ') out[--n] = '\0';
    return n >= minimum;
}

int aprs_encode_object(char *buf, size_t len, const char *name, double lat, double lon,
                       char table, char code, const char *comment, const char *ts_ddhhmm,
                       int kill)
{
    char nm[16], la[9], lo[10];
    comment = comment ? comment : "";
    if (!name || !object_name(name, nm, 1) || !symbol_ok(table, code) || !ts_ddhhmm ||
        !ts_ok(ts_ddhhmm) || !printable(comment, MAX_POSITION_COMMENT, 1) ||
        !aprs_fmt_lat(la, lat) || !aprs_fmt_lon(lo, lon))
        return -1;
    return emit(buf, len, snprintf(buf, len, ";%-9s%c%sz%s%c%s%c%s", nm, kill ? '_' : '*',
                                   ts_ddhhmm, la, table, lo, code, comment));
}

int aprs_encode_item(char *buf, size_t len, const char *name, double lat, double lon,
                     char table, char code, const char *comment, int kill)
{
    char nm[16], la[9], lo[10];
    comment = comment ? comment : "";
    if (!name || !object_name(name, nm, 3) || !symbol_ok(table, code) ||
        !printable(comment, MAX_POSITION_COMMENT, 1) || !aprs_fmt_lat(la, lat) ||
        !aprs_fmt_lon(lo, lon))
        return -1;
    return emit(buf, len, snprintf(buf, len, ")%s%c%s%c%s%c%s", nm, kill ? '_' : '!', la,
                                   table, lo, code, comment));
}
