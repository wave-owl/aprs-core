/*
 * aprs-core -- APRS-IS passcode and server-side filter, version and sizes.
 * See include/aprs.h for sources and licence.
 *
 * Passcode: aprsc src/passcode.c (from aprsd, released by Steve Dimse 2000-04-11): the
 * upper-case base call, at most 9 characters, xored two bytes at a time into 0x73e2, masked
 * to 15 bits. Filter semantics after aprsc src/filter.c (filter_parse and
 * filter_process_one_*), evaluated on a decoded packet:
 *   r/lat/lon/km  a/latN/lonW/latS/lonE  m/km  f/call/km (accepted, never matches)
 *   p/prefix...   b/call...  o/object...  u/destination...  g/addressee...  e/entry...
 *   d/digi...     q/con[/I]  s/pri/alt/overlay  t/poimqstunwc*[/call/km]
 * A word starting with '-' excludes. A packet passes when any positive word matches and no
 * negative word does. Calls take a trailing '*' wildcard (b, o, u, g, e, d).
 */
#include "aprs.h"
#include "aprs_internal.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FILTER_MAX 512
#define WORD_MAX 128
#define MAX_PARTS 16
#define EARTH_KM 6371.0
#define DEG (3.14159265358979323846 / 180.0)

const char *aprs_version(void)
{
    return "aprs-core " APRS_VERSION_STRING " contract=aprs-c1 abi=1 enc=1 filter=1";
}

int aprs_abi(void)
{
    return APRS_ABI;
}

size_t aprs_sizeof(int what)
{
    return what == APRS_SIZEOF_PACKET ? sizeof(aprs_packet) : 0;
}

int aprs_passcode(const char *callsign)
{
    char root[10];
    int n = 0, i;
    unsigned int hash = 0x73e2;
    if (!callsign) return -1;
    while (callsign[n] && callsign[n] != '-' && n < 9) {
        root[n] = (char)toupper((unsigned char)callsign[n]);
        n++;
    }
    root[n] = '\0';
    for (i = 0; i < n; i += 2) {
        hash ^= (unsigned int)(unsigned char)root[i] << 8;
        if (i + 1 < n) hash ^= (unsigned char)root[i + 1];
    }
    return (int)(hash & 0x7fff);
}

/* ---------------------------------------------------------------- helpers */

static void set_err(char *err, size_t len, const char *msg, const char *word)
{
    if (err && len) snprintf(err, len, "%s: %.60s", msg, word);
}

/* Split "x/a/b/c" after the type letter into its parts; returns the count. */
static int split_parts(char *word, char **out)
{
    int n = 0;
    char *s = strchr(word, '/');
    while (s && n < MAX_PARTS) {
        *s++ = '\0';
        out[n++] = s;
        s = strchr(s, '/');
    }
    return n;
}

static int num(const char *s, double *v)
{
    char *end;
    if (!s || !*s) return 0;
    *v = strtod(s, &end);
    return *end == '\0' && !isnan(*v);
}

static int call_ok(const char *s)
{
    size_t n = strlen(s), i;
    if (n == 0 || n > 9) return 0;
    for (i = 0; i < n; i++)
        if (!isalnum((unsigned char)s[i]) && s[i] != '-' && !(s[i] == '*' && i == n - 1))
            return 0;
    return 1;
}

static int prefix_match(const char *s, const char *prefix)
{
    for (; *prefix; s++, prefix++)
        if (!*s || toupper((unsigned char)*s) != toupper((unsigned char)*prefix)) return 0;
    return 1;
}

/* Exact (case-insensitive) match, or a prefix match with a trailing '*'. */
static int call_match(const char *pattern, const char *call)
{
    size_t n = strlen(pattern), i;
    if (n && pattern[n - 1] == '*') {
        for (i = 0; i + 1 < n; i++)
            if (!call[i] || toupper((unsigned char)pattern[i]) != toupper((unsigned char)call[i]))
                return 0;
        return 1;
    }
    for (i = 0; pattern[i] || call[i]; i++)
        if (toupper((unsigned char)pattern[i]) != toupper((unsigned char)call[i])) return 0;
    return 1;
}

static double dist_km(double la1, double lo1, double la2, double lo2)
{
    double p1 = la1 * DEG, p2 = la2 * DEG, dp = p2 - p1, dl = (lo2 - lo1) * DEG;
    double a = sin(dp / 2) * sin(dp / 2) + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
    return 2 * EARTH_KM * asin(a > 1.0 ? 1.0 : sqrt(a));
}

/* Index of the q construct (qA?) in the path, or path_len. */
static int qcon_index(const aprs_packet *p)
{
    int i;
    for (i = 0; i < p->path_len; i++)
        if (p->path[i][0] == 'q' && p->path[i][1] == 'A') return i;
    return p->path_len;
}

#define TB_POSITION 1
#define TB_OBJECT 2
#define TB_ITEM 4
#define TB_MESSAGE 8
#define TB_QUERY 16
#define TB_STATUS 32
#define TB_TELEMETRY 64
#define TB_USERDEF 128
#define TB_NWS 256
#define TB_WX 512
#define TB_CWOP 1024

static int type_bit(char c)
{
    switch (tolower((unsigned char)c)) {
    case 'p': return TB_POSITION;
    case 'o': return TB_OBJECT;
    case 'i': return TB_ITEM;
    case 'm': return TB_MESSAGE;
    case 'q': return TB_QUERY;
    case 's': return TB_STATUS;
    case 't': return TB_TELEMETRY;
    case 'u': return TB_USERDEF;
    case 'n': return TB_NWS;
    case 'w': return TB_WX;
    case 'c': return TB_CWOP;
    case '*': return TB_CWOP - 1;     /* all, CWOP excluded (aprsc) */
    default: return 0;
    }
}

static int packet_types(const aprs_packet *p)
{
    int t = 0;
    switch (p->type) {
    case APRS_T_POSITION: case APRS_T_WAYPOINT: t = TB_POSITION; break;
    case APRS_T_OBJECT: t = TB_OBJECT; break;
    case APRS_T_ITEM: t = TB_ITEM; break;
    case APRS_T_MESSAGE: case APRS_T_ACK: case APRS_T_REJ: case APRS_T_TELEMETRY_META:
        t = TB_MESSAGE;
        break;
    case APRS_T_BULLETIN:
        t = strncmp(p->addressee, "NWS", 3) == 0 ? TB_NWS : TB_MESSAGE;
        break;
    case APRS_T_QUERY: t = TB_QUERY; break;
    case APRS_T_STATUS: t = TB_STATUS; break;
    case APRS_T_TELEMETRY: t = TB_TELEMETRY; break;
    case APRS_T_USER_DEFINED: t = TB_USERDEF; break;
    case APRS_T_WEATHER: t = TB_WX; break;
    default: break;
    }
    if (p->has_weather) t |= TB_WX;
    /* CWOP: weather from CW... and DW... stations (aprsc). */
    if ((t & TB_WX) && (strncmp(p->source, "CW", 2) == 0 || strncmp(p->source, "DW", 2) == 0))
        t = (t & ~TB_WX) | TB_CWOP;
    return t;
}

static int digi_match(const char *pattern, const aprs_packet *p)
{
    int q = qcon_index(p), k, j;
    for (k = 0; k < q; k++) {
        char hop[APRS_CALL_LEN];
        size_t hl;
        strcpy(hop, p->path[k]);
        hl = strlen(hop);
        if (hl && hop[hl - 1] == '*') hop[hl - 1] = '\0';
        if (!call_match(pattern, hop)) continue;
        for (j = k; j < q; j++) {
            size_t l = strlen(p->path[j]);
            if (l && p->path[j][l - 1] == '*') return 1;
        }
        return 0;
    }
    return 0;
}

static int list_match(char type, char **a, int n, const aprs_packet *p)
{
    int i, k;
    for (i = 0; i < n; i++) {
        switch (type) {
        case 'p':
            if (prefix_match(p->source, a[i])) return 1;
            break;
        case 'b':
            if (call_match(a[i], p->source)) return 1;
            break;
        case 'o':
            if ((p->type == APRS_T_OBJECT || p->type == APRS_T_ITEM) && call_match(a[i], p->name))
                return 1;
            break;
        case 'u':
            if (call_match(a[i], p->dest)) return 1;
            break;
        case 'g':
            if (p->type == APRS_T_MESSAGE && call_match(a[i], p->addressee)) return 1;
            break;
        case 'e':
            k = qcon_index(p);
            if (k + 1 < p->path_len && call_match(a[i], p->path[k + 1])) return 1;
            break;
        case 'd':
            if (digi_match(a[i], p)) return 1;
            break;
        default:
            break;
        }
    }
    return 0;
}

/* One filter word (without '-'): 1 match, 0 no match, -1 bad. p NULL: syntax only. */
static int word_eval(const char *word0, const aprs_packet *p, double my_lat, double my_lon,
                     char *err, size_t errlen)
{
    char word[WORD_MAX];
    char *a[MAX_PARTS];
    int n, i;
    double v[4];
    char type;
    if (strlen(word0) >= sizeof(word)) {
        set_err(err, errlen, "filter word too long", word0);
        return -1;
    }
    strcpy(word, word0);
    type = (char)tolower((unsigned char)word[0]);
    if (word[0] == '\0' || word[1] != '/') {
        set_err(err, errlen, "bad filter word", word0);
        return -1;
    }
    n = split_parts(word, a);
    switch (type) {
    case 'r':
        if (n != 3 || !num(a[0], &v[0]) || !num(a[1], &v[1]) || !num(a[2], &v[2]) ||
            fabs(v[0]) > 90 || fabs(v[1]) > 180 || v[2] <= 0) {
            set_err(err, errlen, "r/lat/lon/km expected", word0);
            return -1;
        }
        return p && p->has_position && dist_km(v[0], v[1], p->lat, p->lon) <= v[2];
    case 'a':
        if (n != 4 || !num(a[0], &v[0]) || !num(a[1], &v[1]) || !num(a[2], &v[2]) ||
            !num(a[3], &v[3]) || v[0] < v[2] || v[1] > v[3]) {
            set_err(err, errlen, "a/latN/lonW/latS/lonE expected", word0);
            return -1;
        }
        return p && p->has_position && p->lat <= v[0] && p->lat >= v[2] && p->lon >= v[1] &&
               p->lon <= v[3];
    case 'm':
        if (n != 1 || !num(a[0], &v[0]) || v[0] <= 0) {
            set_err(err, errlen, "m/km expected", word0);
            return -1;
        }
        return p && p->has_position && !isnan(my_lat) && !isnan(my_lon) &&
               dist_km(my_lat, my_lon, p->lat, p->lon) <= v[0];
    case 'f':
        if (n != 2 || !call_ok(a[0]) || !num(a[1], &v[0]) || v[0] <= 0) {
            set_err(err, errlen, "f/call/km expected", word0);
            return -1;
        }
        return 0;
    case 'p': case 'b': case 'o': case 'u': case 'g': case 'e': case 'd':
        if (n < 1) {
            set_err(err, errlen, "at least one call expected", word0);
            return -1;
        }
        for (i = 0; i < n; i++) {
            if (type == 'o' ? (strlen(a[i]) == 0 || strlen(a[i]) > 9) : !call_ok(a[i])) {
                set_err(err, errlen, "bad call in filter", word0);
                return -1;
            }
        }
        return p ? list_match(type, a, n, p) : 0;
    case 'q': {
        int k;
        if (n < 1 || n > 2 || strlen(a[0]) == 0 || (n == 2 && strcmp(a[1], "I") != 0)) {
            set_err(err, errlen, "q/con[/I] expected", word0);
            return -1;
        }
        for (i = 0; a[0][i]; i++) {
            if (!strchr("CXUoOSrRZI", a[0][i])) {
                set_err(err, errlen, "unknown q construct", word0);
                return -1;
            }
        }
        if (!p) return 0;
        k = qcon_index(p);
        return k < p->path_len && p->path[k][2] && strchr(a[0], p->path[k][2]) != NULL;
    }
    case 's':
        if (n < 1 || n > 3) {
            set_err(err, errlen, "s/pri/alt/overlay expected", word0);
            return -1;
        }
        if (!p || !p->symbol_code) return 0;
        if (p->symbol_table == '/') return a[0][0] && strchr(a[0], p->symbol_code) != NULL;
        if (n < 2 || !a[1][0] || !strchr(a[1], p->symbol_code)) return 0;
        if (p->symbol_table == '\\') return 1;
        return n < 3 || !a[2][0] || strchr(a[2], p->symbol_table) != NULL;
    case 't': {
        int mask = 0;
        if (n < 1 || n == 2 || n > 3) {
            set_err(err, errlen, "t/types[/call/km] expected", word0);
            return -1;
        }
        for (i = 0; a[0][i]; i++) {
            int b = type_bit(a[0][i]);
            if (!b) {
                set_err(err, errlen, "unknown type letter", word0);
                return -1;
            }
            mask |= b;
        }
        if (n == 3 && (!call_ok(a[1]) || !num(a[2], &v[0]) || v[0] < 0.1)) {
            set_err(err, errlen, "t/types/call/km: bad call or range", word0);
            return -1;
        }
        if (!p || n == 3) return 0;   /* the /call/km variant needs another station's position */
        return (packet_types(p) & mask) != 0;
    }
    default:
        set_err(err, errlen, "unknown filter type", word0);
        return -1;
    }
}

static int run(const char *spec, const aprs_packet *p, double my_lat, double my_lon,
               char *err, size_t errlen)
{
    char buf[FILTER_MAX];
    char *w;
    int any = 0, positive = 0, negative = 0;
    if (!spec) return -1;
    if (strlen(spec) >= sizeof(buf)) {
        set_err(err, errlen, "filter too long", "");
        return -1;
    }
    strcpy(buf, spec);
    w = buf;
    while (*w) {
        char *end;
        int neg, r;
        while (*w == ' ') w++;
        if (!*w) break;
        end = strchr(w, ' ');
        if (end) *end = '\0';
        neg = w[0] == '-';
        r = word_eval(w + neg, p, my_lat, my_lon, err, errlen);
        if (r < 0) return -1;
        if (neg) negative |= r;
        else positive |= r;
        any = 1;
        if (!end) break;
        w = end + 1;
    }
    if (!any) return 0;
    return positive && !negative;
}

int aprs_filter_check(const char *spec, char *err, size_t errlen)
{
    size_t i;
    if (err && errlen) err[0] = '\0';
    if (!spec) return -1;
    for (i = 0; spec[i]; i++) {
        if ((unsigned char)spec[i] < 0x20 || (unsigned char)spec[i] > 0x7e) {
            set_err(err, errlen, "filter must be printable ASCII", "");
            return -1;
        }
    }
    return run(spec, NULL, NAN, NAN, err, errlen) < 0 ? -1 : 0;
}

int aprs_filter_match(const char *spec, const aprs_packet *p, double my_lat, double my_lon)
{
    if (!p) return -1;
    return run(spec, p, my_lat, my_lon, NULL, 0);
}
