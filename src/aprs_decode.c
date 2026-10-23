/*
 * aprs-core -- packet decoder. See include/aprs.h for sources and licence.
 *
 * Structure after libfap's fap_parseaprs() (src/fap.c) and helpers.c: header split, third
 * party unwrap, dispatch on the data type identifier, then position / object / item /
 * Mic-E / message / status / weather / telemetry / NMEA parsers. The regular expressions
 * of libfap are replaced by explicit character checks.
 */
#include "aprs.h"
#include "aprs_internal.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define KNOT_KMH 1.852
#define MPH_KMH 1.609344
#define FOOT_M 0.3048
#define MILE_KM 1.609344

/* ---------------------------------------------------------------- helpers */

static void copy_str(char *dst, size_t cap, const char *src, size_t n)
{
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void rstrip(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = '\0';
}

static const char *lstrip(const char *s)
{
    while (*s == ' ') s++;
    return s;
}

static int all_digits(const char *s, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
        if (!isdigit((unsigned char)s[i])) return 0;
    return 1;
}

static long base91(const char *s, int n)
{
    long v = 0;
    int i;
    for (i = 0; i < n; i++) v = v * 91 + ((unsigned char)s[i] - 33);
    return v;
}

static int valid_ll(double lat, double lon)
{
    return lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0;
}

static double rnd(double v, double scale)
{
    return floor(v * scale + 0.5) / scale;
}

static void init_packet(aprs_packet *p)
{
    int i;
    memset(p, 0, sizeof(*p));
    p->lat = p->lon = NAN;
    p->course_deg = -1;
    p->speed_kmh = p->altitude_m = p->range_km = p->phg_height_m = NAN;
    p->phg_power_w = p->phg_gain_db = p->phg_dir_deg = -1;
    p->wx_wind_dir_deg = p->wx_wind_speed_kmh = p->wx_wind_gust_kmh = p->wx_temp_c = NAN;
    p->wx_rain_1h_mm = p->wx_rain_24h_mm = p->wx_rain_midnight_mm = NAN;
    p->wx_humidity_pct = p->wx_pressure_hpa = p->wx_luminosity_wm2 = p->wx_snow_24h_cm = NAN;
    for (i = 0; i < 5; i++) p->tlm_analog[i] = NAN;
}

/* A number of width n ("..." / spaces / dashes = absent). */
static double field_num(const char *s, size_t n)
{
    char buf[16];
    size_t i;
    int any = 0;
    char *end;
    double v;
    if (n == 0 || n >= sizeof(buf)) return NAN;
    for (i = 0; i < n; i++) {
        if (s[i] == '\0') return NAN;
        if (isdigit((unsigned char)s[i])) any = 1;
    }
    if (!any) return NAN;
    memcpy(buf, s, n);
    buf[n] = '\0';
    v = strtod(lstrip(buf), &end);
    if (*lstrip(end) != '\0') return NAN;
    return v;
}

/* ---------------------------------------------------------------- weather (ch. 12) */

static int wx_width(char c)
{
    switch (c) {
    case 'c': case 's': case 'g': case 't': case 'r': case 'p': case 'P': case 'L':
    case 'l': case '#':
        return 3;
    case 'h':
        return 2;
    case 'b':
        return 5;
    default:
        return 0;
    }
}

static void wx_fields(aprs_packet *p, const char *s, int positionless)
{
    while (*s) {
        int w = wx_width(*s);
        char key = *s;
        size_t avail = strlen(s + 1);
        double v;
        if (!w || avail < (size_t)w) break;
        v = field_num(s + 1, (size_t)w);
        s += 1 + w;
        if (isnan(v)) continue;
        p->has_weather = 1;
        switch (key) {
        case 'c': p->wx_wind_dir_deg = v; break;
        case 's':
            if (positionless) p->wx_wind_speed_kmh = rnd(v * MPH_KMH, 10);
            else p->wx_snow_24h_cm = rnd(v * 2.54, 10);
            break;
        case 'g': p->wx_wind_gust_kmh = rnd(v * MPH_KMH, 10); break;
        case 't': p->wx_temp_c = rnd((v - 32.0) * 5.0 / 9.0, 10); break;
        case 'r': p->wx_rain_1h_mm = rnd(v * 0.254, 10); break;
        case 'p': p->wx_rain_24h_mm = rnd(v * 0.254, 10); break;
        case 'P': p->wx_rain_midnight_mm = rnd(v * 0.254, 10); break;
        case 'h': p->wx_humidity_pct = v == 0 ? 100.0 : v; break;
        case 'b': p->wx_pressure_hpa = rnd(v / 10.0, 10); break;
        case 'L': p->wx_luminosity_wm2 = v; break;
        case 'l': p->wx_luminosity_wm2 = v + 1000.0; break;
        default: break;
        }
    }
}

/* ---------------------------------------------------------------- comment extensions */

static int is_cse_spd(const char *s)
{
    return strlen(s) >= 7 && all_digits(s, 3) && s[3] == '/' && all_digits(s + 4, 3);
}

static const char *apply_extension(aprs_packet *p, const char *rest)
{
    if (is_cse_spd(rest) && p->symbol_code != '_') {
        int course = atoi(rest), speed = atoi(rest + 4);
        if (course > 0 && course <= 360) p->course_deg = course % 360;
        p->speed_kmh = rnd(speed * KNOT_KMH, 10);
        return rest + 7;
    }
    if (strncmp(rest, "PHG", 3) == 0 && strlen(rest) >= 7 && all_digits(rest + 3, 4)) {
        int pw = rest[3] - '0', h = rest[4] - '0', g = rest[5] - '0', d = rest[6] - '0';
        p->phg_power_w = pw * pw;
        p->phg_height_m = rnd(10.0 * pow(2.0, h) * FOOT_M, 10);
        p->phg_gain_db = g;
        p->phg_dir_deg = d * 45;
        return rest + 7;
    }
    if (strncmp(rest, "RNG", 3) == 0 && strlen(rest) >= 7 && all_digits(rest + 3, 4)) {
        p->range_km = rnd(atoi(rest + 3) * MILE_KM, 10);
        return rest + 7;
    }
    return rest;
}

/* Altitude "/A=dddddd" anywhere in the comment (removed), DAO "!Xyz!" removed. */
static void finish_comment(aprs_packet *p, const char *rest)
{
    char buf[APRS_TEXT_LEN];
    char *a, *d;
    copy_str(buf, sizeof(buf), rest, strlen(rest));
    a = strstr(buf, "/A=");
    if (a) {
        char *q = a + 3;
        int neg = *q == '-';
        int n = 0;
        if (neg) q++;
        while (isdigit((unsigned char)q[n]) && n < 6) n++;
        if (n + neg >= 5 && n + neg <= 6) {
            p->altitude_m = rnd(atol(a + 3) * FOOT_M, 10);
            memmove(a, q + n, strlen(q + n) + 1);
        }
    }
    d = buf;
    while ((d = strchr(d, '!')) != NULL) {
        if (strlen(d) >= 5 && d[4] == '!' && (isalpha((unsigned char)d[1]) || d[1] == ' ')) {
            memmove(d, d + 5, strlen(d + 5) + 1);
        } else {
            d++;
        }
    }
    copy_str(p->comment, sizeof(p->comment), lstrip(buf), strlen(lstrip(buf)));
    rstrip(p->comment);
}

/* ---------------------------------------------------------------- positions */

/* Position ambiguity as libfap: blanked digits; the first blanked digit becomes the
 * midpoint ('5', or '3' for the tens of minutes), the rest 0. Returns -1 if invalid. */
static int unblank(char *s, int ndigits_from, int len)
{
    int i, amb = 0, first = 1;
    for (i = ndigits_from; i < len; i++) {
        if (s[i] == ' ') {
            amb++;
            if (first) {
                s[i] = (i == ndigits_from) ? '3' : '5';
                first = 0;
            } else {
                s[i] = '0';
            }
        } else if (amb && s[i] != '.') {
            return -1;      /* digits after a blank */
        }
    }
    return amb;
}

static int parse_uncompressed(aprs_packet *p, const char *body)
{
    char lat_s[9], lon_s[10];
    double lat, lon, lat_min, lon_min;
    int amb;
    const char *rest;
    if (strlen(body) < 19) return 0;
    if (!isdigit((unsigned char)body[0]) || !isdigit((unsigned char)body[1]) || body[4] != '.'
        || strchr("NnSs", body[7]) == NULL || body[7] == '\0')
        return 0;
    if (!all_digits(body + 9, 3) || body[14] != '.' || strchr("EeWw", body[17]) == NULL
        || body[17] == '\0')
        return 0;
    memcpy(lat_s, body, 8);
    lat_s[8] = '\0';
    memcpy(lon_s, body + 9, 9);
    lon_s[9] = '\0';
    p->format = APRS_F_UNCOMPRESSED;
    p->symbol_table = body[8];
    p->symbol_code = body[18];
    amb = unblank(lat_s, 2, 7);
    if (amb >= 0 && unblank(lon_s, 3, 8) >= 0) {
        int ok = isdigit((unsigned char)lat_s[2]) && isdigit((unsigned char)lat_s[3]) &&
                 isdigit((unsigned char)lat_s[5]) && isdigit((unsigned char)lat_s[6]) &&
                 isdigit((unsigned char)lon_s[3]) && isdigit((unsigned char)lon_s[4]) &&
                 isdigit((unsigned char)lon_s[6]) && isdigit((unsigned char)lon_s[7]);
        lat_min = atof(lat_s + 2);
        lon_min = atof(lon_s + 3);
        lat = (lat_s[0] - '0') * 10 + (lat_s[1] - '0') + lat_min / 60.0;
        lon = (lon_s[0] - '0') * 100 + (lon_s[1] - '0') * 10 + (lon_s[2] - '0') + lon_min / 60.0;
        if (lat_s[7] == 'S' || lat_s[7] == 's') lat = -lat;
        if (lon_s[8] == 'W' || lon_s[8] == 'w') lon = -lon;
        if (ok && lat_min < 60.0 && lon_min < 60.0 && valid_ll(lat, lon)) {
            p->has_position = 1;
            p->lat = rnd(lat, 1e6);
            p->lon = rnd(lon, 1e6);
            p->pos_ambiguity = amb;
        }
    }
    rest = body + 19;
    if (p->symbol_code == '_') {
        if (is_cse_spd(rest)) {
            double dir = field_num(rest, 3), spd = field_num(rest + 4, 3);
            p->has_weather = 1;
            p->wx_wind_dir_deg = dir;
            p->wx_wind_speed_kmh = isnan(spd) ? NAN : rnd(spd * MPH_KMH, 10);
            wx_fields(p, rest + 7, 0);
            rest = "";
        } else if (strlen(rest) >= 7 && rest[3] == '/') {
            /* "..."/"   " wind fields */
            p->has_weather = 1;
            wx_fields(p, rest + 7, 0);
            rest = "";
        } else {
            wx_fields(p, rest, 0);
        }
    }
    rest = apply_extension(p, rest);
    finish_comment(p, rest);
    return 1;
}

static int parse_compressed(aprs_packet *p, const char *body)
{
    char table = body[0];
    double lat, lon;
    int i;
    unsigned char c, s, t;
    if (strlen(body) < 13) return 0;
    if (!(table == '/' || table == '\\' || (table >= 'A' && table <= 'Z') ||
          (table >= 'a' && table <= 'j')))
        return 0;
    for (i = 1; i < 9; i++)
        if ((unsigned char)body[i] < 33 || (unsigned char)body[i] > 123) return 0;
    lat = 90.0 - base91(body + 1, 4) / 380926.0;
    lon = -180.0 + base91(body + 5, 4) / 190463.0;
    if (!valid_ll(lat, lon)) return 0;
    p->format = APRS_F_COMPRESSED;
    p->has_position = 1;
    p->lat = rnd(lat, 1e6);
    p->lon = rnd(lon, 1e6);
    p->symbol_table = (table >= 'a' && table <= 'j') ? (char)('0' + table - 'a') : table;
    p->symbol_code = body[9];
    c = (unsigned char)body[10];
    s = (unsigned char)body[11];
    t = (unsigned char)body[12];
    if (c != ' ') {
        if (((t - 33) & 0x18) == 0x10) {
            p->altitude_m = rnd(pow(1.002, (c - 33) * 91 + (s - 33)) * FOOT_M, 10);
        } else if (c == '{') {
            p->range_km = rnd(2.0 * pow(1.08, s - 33) * MILE_KM, 10);
        } else if (c >= '!' && c <= 'z') {
            p->course_deg = ((c - 33) * 4) % 360;
            p->speed_kmh = rnd((pow(1.08, s - 33) - 1.0) * KNOT_KMH, 10);
        }
    }
    if (p->symbol_code == '_') wx_fields(p, body + 13, 0);
    finish_comment(p, body + 13);
    return 1;
}

static int parse_position_body(aprs_packet *p, const char *body)
{
    if (isdigit((unsigned char)body[0])) return parse_uncompressed(p, body);
    return parse_compressed(p, body);
}

/* ---------------------------------------------------------------- Mic-E (ch. 10) */

static const char *const MICE_STD[8] = {
    "emergency", "priority", "special", "committed", "returning", "in service", "en route",
    "off duty"};

static void parse_mice(aprs_packet *p, const char *info)
{
    char dest[8];
    int digits[6], std[6], cus[6];
    int i, n, b[6], lon_deg, lon_min, speed, course;
    double lat, lon;
    const char *rest;
    n = 0;
    while (p->dest[n] && p->dest[n] != '-' && n < 7) n++;
    if (n != 6 || strlen(info) < 9) return;
    copy_str(dest, sizeof(dest), p->dest, 6);
    for (i = 0; i < 6; i++) {
        char ch = (char)toupper((unsigned char)dest[i]);
        if (ch >= '0' && ch <= '9') { digits[i] = ch - '0'; std[i] = 0; cus[i] = 0; }
        else if (ch >= 'A' && ch <= 'J') { digits[i] = ch - 'A'; std[i] = 0; cus[i] = 1; }
        else if (ch >= 'P' && ch <= 'Y') { digits[i] = ch - 'P'; std[i] = 1; cus[i] = 0; }
        else if (ch == 'K' || ch == 'L' || ch == 'Z') {
            digits[i] = -1;      /* ambiguity */
            std[i] = ch == 'Z';
            cus[i] = ch == 'K';
        } else {
            return;
        }
        dest[i] = ch;
    }
    {
        /* Ambiguity from the right: blanked minute digits become the midpoint (libfap). */
        int amb = 0, first = 1;
        for (i = 2; i < 6; i++) {
            if (digits[i] < 0) amb++;
        }
        for (i = 2; i < 6; i++) {
            if (digits[i] < 0) {
                digits[i] = first ? (i == 2 ? 3 : 5) : 0;
                first = 0;
            }
        }
        if (digits[0] < 0 || digits[1] < 0) return;
        p->pos_ambiguity = amb;
    }
    lat = digits[0] * 10 + digits[1] +
          (digits[2] * 10 + digits[3] + (digits[4] * 10 + digits[5]) / 100.0) / 60.0;
    if (dest[3] < 'P') lat = -lat;
    for (i = 0; i < 6; i++) b[i] = (unsigned char)info[1 + i] - 28;
    lon_deg = b[0] + (dest[4] >= 'P' ? 100 : 0);
    if (lon_deg >= 180 && lon_deg <= 189) lon_deg -= 80;
    else if (lon_deg >= 190 && lon_deg <= 199) lon_deg -= 190;
    lon_min = b[1] >= 60 ? b[1] - 60 : b[1];
    if (lon_min < 0 || lon_min >= 60 || b[2] < 0 || b[2] > 99) return;
    lon = lon_deg + (lon_min + b[2] / 100.0) / 60.0;
    if (dest[5] >= 'P') lon = -lon;
    if (!valid_ll(lat, lon)) return;
    p->type = APRS_T_POSITION;
    p->format = APRS_F_MICE;
    p->has_position = 1;
    p->lat = rnd(lat, 1e6);
    p->lon = rnd(lon, 1e6);
    speed = b[3] * 10 + b[4] / 10;
    if (speed >= 800) speed -= 800;
    course = (b[4] % 10) * 100 + b[5];
    if (course >= 400) course -= 400;
    p->speed_kmh = rnd(speed * KNOT_KMH, 10);
    if (course > 0 && course <= 360) p->course_deg = course % 360;
    p->symbol_code = info[7];
    p->symbol_table = info[8];
    if (cus[0] || cus[1] || cus[2]) {
        int k = 7 - (cus[0] * 4 + cus[1] * 2 + cus[2]);
        copy_str(p->mic_e_message, sizeof(p->mic_e_message), "custom ", 7);
        p->mic_e_message[7] = (char)('0' + k);
        p->mic_e_message[8] = '\0';
    } else {
        const char *m = MICE_STD[std[0] * 4 + std[1] * 2 + std[2]];
        copy_str(p->mic_e_message, sizeof(p->mic_e_message), m, strlen(m));
    }
    rest = info + 9;
    if (*rest == '>' || *rest == ']' || *rest == '`' || *rest == '\'') rest++;
    if (strlen(rest) >= 4 && rest[3] == '}') {
        int ok = 1;
        for (i = 0; i < 3; i++)
            if ((unsigned char)rest[i] < 33 || (unsigned char)rest[i] > 123) ok = 0;
        if (ok) {
            p->altitude_m = (double)(base91(rest, 3) - 10000);
            rest += 4;
        }
    }
    finish_comment(p, rest);
}

/* ---------------------------------------------------------------- NMEA (ch. 5) */

static int nmea_latlon(const char *lat_s, const char *ns, const char *lon_s, const char *ew,
                       double *lat, double *lon)
{
    if (strlen(lat_s) < 4 || strlen(lon_s) < 5) return 0;
    *lat = (lat_s[0] - '0') * 10 + (lat_s[1] - '0') + atof(lat_s + 2) / 60.0;
    *lon = (lon_s[0] - '0') * 100 + (lon_s[1] - '0') * 10 + (lon_s[2] - '0') +
           atof(lon_s + 3) / 60.0;
    if (!isdigit((unsigned char)lat_s[0]) || !isdigit((unsigned char)lon_s[0])) return 0;
    if (ns[0] == 'S' || ns[0] == 's') *lat = -*lat;
    if (ew[0] == 'W' || ew[0] == 'w') *lon = -*lon;
    return valid_ll(*lat, *lon);
}

static void parse_nmea(aprs_packet *p, const char *info)
{
    char buf[APRS_INFO_LEN];
    char *f[16];
    int nf = 0;
    char *s, *star;
    double lat, lon;
    int ok = 0;
    copy_str(buf, sizeof(buf), info, strlen(info));
    star = strchr(buf, '*');
    if (star) *star = '\0';
    s = buf;
    while (nf < 16) {
        f[nf++] = s;
        s = strchr(s, ',');
        if (!s) break;
        *s++ = '\0';
    }
    if (strlen(f[0]) < 6) return;
    if (strcmp(f[0] + 3, "WPL") == 0 && nf >= 6) {
        ok = nmea_latlon(f[1], f[2], f[3], f[4], &lat, &lon);
        if (ok) {
            p->type = APRS_T_WAYPOINT;
            copy_str(p->name, 10, lstrip(f[5]), strlen(lstrip(f[5])));
            rstrip(p->name);
        }
    } else if (strcmp(f[0] + 3, "RMC") == 0 && nf >= 7 && strcmp(f[2], "A") == 0) {
        ok = nmea_latlon(f[3], f[4], f[5], f[6], &lat, &lon);
        if (ok) {
            p->type = APRS_T_POSITION;
            if (nf >= 9) {
                double spd = field_num(f[7], strlen(f[7])), crs = field_num(f[8], strlen(f[8]));
                if (!isnan(spd)) p->speed_kmh = rnd(spd * KNOT_KMH, 10);
                if (!isnan(crs)) p->course_deg = ((int)crs) % 360;
            }
        }
    } else if (strcmp(f[0] + 3, "GGA") == 0 && nf >= 6) {
        ok = nmea_latlon(f[2], f[3], f[4], f[5], &lat, &lon);
        if (ok) p->type = APRS_T_POSITION;
    }
    if (!ok) {
        p->type = APRS_T_UNKNOWN;
        return;
    }
    p->format = APRS_F_NMEA;
    p->has_position = 1;
    p->lat = rnd(lat, 1e6);
    p->lon = rnd(lon, 1e6);
    p->symbol_table = '/';
    p->symbol_code = '/';
}

/* ---------------------------------------------------------------- messages (ch. 14) */

static int is_alnum_str(const char *s, size_t n)
{
    size_t i;
    if (n == 0) return 0;
    for (i = 0; i < n; i++)
        if (!isalnum((unsigned char)s[i])) return 0;
    return 1;
}

static void parse_message(aprs_packet *p, const char *info)
{
    char body[APRS_TEXT_LEN];
    size_t n;
    char *brace;
    static const char *const META[4] = {"PARM.", "UNIT.", "EQNS.", "BITS."};
    int i;
    if (strlen(info) < 11 || info[10] != ':') return;
    copy_str(p->addressee, sizeof(p->addressee), info + 1, 9);
    rstrip(p->addressee);
    for (i = 0; p->addressee[i]; i++)
        p->addressee[i] = (char)toupper((unsigned char)p->addressee[i]);
    copy_str(body, sizeof(body), info + 11, strlen(info + 11));
    n = strlen(body);
    while (n && (body[n - 1] == '\r' || body[n - 1] == '\n')) body[--n] = '\0';
    /* ack / rej: "ackNNNNN" with an optional reply-ack "}..." */
    if ((strncmp(body, "ack", 3) == 0 || strncmp(body, "rej", 3) == 0)) {
        size_t k = 3;
        while (k < n && k < 8 && isalnum((unsigned char)body[k])) k++;
        if (k > 3 && (k == n || body[k] == '}' || body[k] == ' ')) {
            size_t j = k;
            if (j < n && body[j] == '}') {
                j++;
                while (j < n && isalnum((unsigned char)body[j])) j++;
            }
            while (j < n && body[j] == ' ') j++;
            if (j == n) {
                p->type = body[0] == 'a' ? APRS_T_ACK : APRS_T_REJ;
                copy_str(p->acked_msgno, sizeof(p->acked_msgno), body + 3, k - 3);
                return;
            }
        }
    }
    if (strncmp(p->addressee, "BLN", 3) == 0 || strncmp(p->addressee, "NWS", 3) == 0) {
        p->type = APRS_T_BULLETIN;
        copy_str(p->message_text, sizeof(p->message_text), body, n);
        return;
    }
    for (i = 0; i < 4; i++) {
        if (strncmp(body, META[i], 5) == 0) {
            p->type = APRS_T_TELEMETRY_META;
            copy_str(p->tlm_meta_kind, sizeof(p->tlm_meta_kind), META[i], 4);
            copy_str(p->message_text, sizeof(p->message_text), body + 5, n - 5);
            return;
        }
    }
    p->type = APRS_T_MESSAGE;
    brace = strrchr(body, '{');
    if (brace) {
        char *tail = brace + 1;
        char *close = strchr(tail, '}');
        size_t tn = strlen(tail);
        while (tn && tail[tn - 1] == ' ') tail[--tn] = '\0';
        if (close) {
            /* reply-ack "{MM}AA" */
            size_t mm = (size_t)(close - tail), aa = strlen(close + 1);
            if (mm >= 1 && mm <= 5 && is_alnum_str(tail, mm) && aa <= 5 &&
                (aa == 0 || is_alnum_str(close + 1, aa))) {
                copy_str(p->msgno, sizeof(p->msgno), tail, mm);
                copy_str(p->reply_ack, sizeof(p->reply_ack), close + 1, aa);
                *brace = '\0';
            }
        } else if (tn >= 1 && tn <= 5 && is_alnum_str(tail, tn)) {
            copy_str(p->msgno, sizeof(p->msgno), tail, tn);
            *brace = '\0';
        }
    }
    copy_str(p->message_text, sizeof(p->message_text), body, strlen(body));
}

/* ---------------------------------------------------------------- the rest */

static void parse_telemetry(aprs_packet *p, const char *info)
{
    /* T#sss,a1,a2,a3,a4,a5,bbbbbbbb */
    const char *s = info + 2;
    const char *comma;
    int i;
    size_t n;
    comma = strchr(s, ',');
    if (!comma || comma == s || comma - s > 5) return;
    copy_str(p->tlm_seq, sizeof(p->tlm_seq), s, (size_t)(comma - s));
    s = comma + 1;
    for (i = 0; i < 5; i++) {
        const char *e = strchr(s, ',');
        size_t len = e ? (size_t)(e - s) : strlen(s);
        if (!e && i < 4) return;
        p->tlm_analog[i] = field_num(s, len);
        s = e ? e + 1 : s + len;
    }
    n = 0;
    while (n < 8 && (s[n] == '0' || s[n] == '1')) n++;
    copy_str(p->tlm_digital, sizeof(p->tlm_digital), s, n);
    p->type = APRS_T_TELEMETRY;
    p->has_telemetry = 1;
    finish_comment(p, s + n);
}

static void parse_status(aprs_packet *p, const char *info)
{
    const char *body = info + 1;
    if (strlen(body) >= 7 && all_digits(body, 6) && body[6] == 'z') body += 7;
    p->type = APRS_T_STATUS;
    copy_str(p->status, sizeof(p->status), lstrip(body), strlen(lstrip(body)));
    rstrip(p->status);
}

static void parse_object(aprs_packet *p, const char *info)
{
    if (strlen(info) < 18 || (info[10] != '*' && info[10] != '_')) return;
    if (!all_digits(info + 11, 6) || strchr("zh/", info[17]) == NULL || info[17] == '\0')
        return;
    p->type = APRS_T_OBJECT;
    copy_str(p->name, 10, info + 1, 9);
    rstrip(p->name);
    p->killed = info[10] == '_';
    parse_position_body(p, info + 18);
}

static void parse_item(aprs_packet *p, const char *info)
{
    const char *body = info + 1;
    size_t i, n = strlen(body);
    for (i = 3; i < 10 && i < n; i++) {
        if (body[i] == '!' || body[i] == '_') {
            p->type = APRS_T_ITEM;
            copy_str(p->name, 10, body, i);
            rstrip(p->name);
            p->killed = body[i] == '_';
            parse_position_body(p, body + i + 1);
            return;
        }
    }
}

static int is_timestamp(const char *s)
{
    if (strlen(s) < 7) return 0;
    return all_digits(s, 6) && (s[6] == 'z' || s[6] == 'h' || s[6] == '/');
}

static void decode_info(aprs_packet *p, const char *info)
{
    char first = info[0];
    switch (first) {
    case '!': case '=':
        p->type = APRS_T_POSITION;
        p->messaging = first == '=';
        if (!parse_position_body(p, info + 1)) p->type = APRS_T_UNKNOWN;
        break;
    case '/': case '@':
        p->type = APRS_T_POSITION;
        p->messaging = first == '@';
        if (!is_timestamp(info + 1) || !parse_position_body(p, info + 8))
            p->type = APRS_T_UNKNOWN;
        break;
    case '`': case '\'': case 0x1c: case 0x1d:
        parse_mice(p, info);
        break;
    case ';':
        parse_object(p, info);
        break;
    case ')':
        parse_item(p, info);
        break;
    case ':':
        parse_message(p, info);
        break;
    case '>':
        parse_status(p, info);
        break;
    case '_':
        if (strlen(info) >= 9 && all_digits(info + 1, 8)) {
            p->type = APRS_T_WEATHER;
            wx_fields(p, info + 9, 1);
        }
        break;
    case 'T':
        if (info[1] == '#') parse_telemetry(p, info);
        break;
    case '$':
        parse_nmea(p, info);
        break;
    case '?':
        p->type = APRS_T_QUERY;
        break;
    case '<':
        p->type = APRS_T_CAPABILITIES;
        break;
    case '{':
        p->type = APRS_T_USER_DEFINED;
        break;
    default:
        break;
    }
    /* A weather-station position carries its weather; a position whose body did not parse
     * keeps type position only if a format was recognised (garbage coordinates). */
}

/* Split "SRC>DEST,P1,P2:info". Returns pointer to info or NULL. */
static const char *split_header(aprs_packet *p, const char *line)
{
    const char *gt, *colon, *s;
    size_t n;
    gt = strchr(line, '>');
    colon = strchr(line, ':');
    if (!gt || !colon || gt > colon) return NULL;
    n = (size_t)(gt - line);
    if (n == 0 || n > 9 + 3) return NULL;
    for (s = line; s < gt; s++)
        if (!isalnum((unsigned char)*s) && *s != '-') return NULL;
    copy_str(p->source, sizeof(p->source), line, n);
    s = gt + 1;
    {
        int idx = -1;
        while (s < colon) {
            const char *e = memchr(s, ',', (size_t)(colon - s));
            size_t len = e ? (size_t)(e - s) : (size_t)(colon - s);
            if (len == 0 || len >= APRS_CALL_LEN) return NULL;
            if (idx < 0) {
                copy_str(p->dest, sizeof(p->dest), s, len);
            } else if (idx < APRS_MAX_PATH) {
                copy_str(p->path[idx], APRS_CALL_LEN, s, len);
                p->path_len = idx + 1;
            }
            idx++;
            s = e ? e + 1 : colon;
        }
        if (idx < 0) return NULL;
    }
    {
        int i;
        for (i = 0; p->source[i]; i++)
            p->source[i] = (char)toupper((unsigned char)p->source[i]);
        for (i = 0; p->dest[i]; i++) p->dest[i] = (char)toupper((unsigned char)p->dest[i]);
    }
    return colon + 1;
}

static int decode_line(const char *line, aprs_packet *out, int depth)
{
    char buf[APRS_INFO_LEN + 64];
    const char *info;
    size_t n = strlen(line);
    while (n && (line[n - 1] == '\r' || line[n - 1] == '\n')) n--;
    if (n == 0 || n >= sizeof(buf)) return -1;
    memcpy(buf, line, n);
    buf[n] = '\0';
    init_packet(out);
    info = split_header(out, buf);
    if (!info) return -1;
    if (info[0] == '}' && depth == 0) {
        aprs_packet inner;
        if (decode_line(info + 1, &inner, 1) == 0) {
            *out = inner;
            out->third_party = 1;
            return 0;
        }
    }
    if (*info) decode_info(out, info);
    return 0;
}

int aprs_decode(const char *line, aprs_packet *out)
{
    if (!line || !out) return -1;
    return decode_line(line, out, 0);
}
