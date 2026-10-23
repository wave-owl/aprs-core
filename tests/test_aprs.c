/* aprs-core C tests: oracle vectors (tests/vectors/decode.tsv), encoders, passcode, filter. */
#include "aprs.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
static int checks = 0;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); \
    fputc('\n', stderr); } } while (0)

static const char *type_name(int t)
{
    static const char *const names[] = {"unknown", "position", "object", "item", "waypoint",
                                        "message", "ack", "rej", "bulletin", "status",
                                        "weather", "telemetry", "telemetry-meta", "query",
                                        "capabilities", "user-defined"};
    return t >= 0 && t <= 15 ? names[t] : "?";
}

static int near(double a, double b, double tol)
{
    return !isnan(a) && fabs(a - b) <= tol;
}

static void check_field(const char *line, const aprs_packet *p, const char *key,
                        const char *val)
{
    double v = atof(val);
    if (!strcmp(key, "oracle") || !strcmp(key, "why")) return;
    if (!strcmp(key, "type")) {
        CHECK(!strcmp(type_name(p->type), val), "%s: type %s != %s", line, type_name(p->type), val);
    } else if (!strcmp(key, "lat")) {
        CHECK(p->has_position && near(p->lat, v, 2e-5), "%s: lat %f != %s", line, p->lat, val);
    } else if (!strcmp(key, "lon")) {
        CHECK(p->has_position && near(p->lon, v, 2e-5), "%s: lon %f != %s", line, p->lon, val);
    } else if (!strcmp(key, "nopos")) {
        CHECK(!p->has_position, "%s: unexpected position", line);
    } else if (!strcmp(key, "sym")) {
        CHECK(p->symbol_table == val[0] && p->symbol_code == val[1], "%s: sym %c%c != %s", line,
              p->symbol_table, p->symbol_code, val);
    } else if (!strcmp(key, "course")) {
        CHECK(p->course_deg == (int)v, "%s: course %d != %s", line, p->course_deg, val);
    } else if (!strcmp(key, "speed")) {
        CHECK(near(p->speed_kmh, v, 0.06), "%s: speed %f != %s", line, p->speed_kmh, val);
    } else if (!strcmp(key, "alt")) {
        CHECK(near(p->altitude_m, v, 0.6), "%s: alt %f != %s", line, p->altitude_m, val);
    } else if (!strcmp(key, "name")) {
        CHECK(!strcmp(p->name, val), "%s: name '%s' != '%s'", line, p->name, val);
    } else if (!strcmp(key, "alive")) {
        CHECK(p->killed == !(int)v, "%s: killed %d, alive %s", line, p->killed, val);
    } else if (!strcmp(key, "msgcap")) {
        CHECK(p->messaging == (int)v, "%s: messaging %d != %s", line, p->messaging, val);
    } else if (!strcmp(key, "to")) {
        CHECK(!strcmp(p->addressee, val), "%s: to '%s' != '%s'", line, p->addressee, val);
    } else if (!strcmp(key, "text")) {
        CHECK(!strcmp(p->message_text, val), "%s: text '%s' != '%s'", line, p->message_text, val);
    } else if (!strcmp(key, "msgno")) {
        CHECK(!strcmp(p->msgno, val), "%s: msgno '%s' != '%s'", line, p->msgno, val);
    } else if (!strcmp(key, "acked")) {
        CHECK(!strcmp(p->acked_msgno, val), "%s: acked '%s' != '%s'", line, p->acked_msgno, val);
    } else if (!strcmp(key, "status")) {
        CHECK(!strcmp(p->status, val), "%s: status '%s' != '%s'", line, p->status, val);
    } else if (!strcmp(key, "temp")) {
        CHECK(near(p->wx_temp_c, v, 0.06), "%s: temp %f != %s", line, p->wx_temp_c, val);
    } else if (!strcmp(key, "hum")) {
        CHECK(near(p->wx_humidity_pct, v, 0.01), "%s: hum %f != %s", line, p->wx_humidity_pct, val);
    } else if (!strcmp(key, "press")) {
        CHECK(near(p->wx_pressure_hpa, v, 0.06), "%s: press %f != %s", line, p->wx_pressure_hpa, val);
    } else if (!strcmp(key, "rain1h")) {
        CHECK(near(p->wx_rain_1h_mm, v, 0.06), "%s: rain %f != %s", line, p->wx_rain_1h_mm, val);
    } else if (!strcmp(key, "wind")) {
        CHECK(near(p->wx_wind_dir_deg, v, 0.01), "%s: wind %f != %s", line, p->wx_wind_dir_deg, val);
    } else if (!strcmp(key, "seq")) {
        CHECK(!strcmp(p->tlm_seq, val), "%s: seq '%s' != '%s'", line, p->tlm_seq, val);
    } else if (!strcmp(key, "a1")) {
        CHECK(near(p->tlm_analog[0], v, 1e-9), "%s: a1 %f != %s", line, p->tlm_analog[0], val);
    } else if (!strcmp(key, "bits")) {
        CHECK(!strcmp(p->tlm_digital, val), "%s: bits '%s' != '%s'", line, p->tlm_digital, val);
    } else if (!strcmp(key, "meta")) {
        CHECK(!strcmp(p->tlm_meta_kind, val), "%s: meta '%s' != '%s'", line, p->tlm_meta_kind, val);
    } else if (!strcmp(key, "src")) {
        CHECK(!strcmp(p->source, val), "%s: source '%s' != '%s'", line, p->source, val);
    } else if (!strcmp(key, "third")) {
        CHECK(p->third_party == (int)v, "%s: third %d", line, p->third_party);
    } else if (!strcmp(key, "phg")) {
        CHECK(p->phg_power_w == (int)v, "%s: phg power %d != %s", line, p->phg_power_w, val);
    } else if (!strcmp(key, "rng")) {
        CHECK(near(p->range_km, v, 0.06), "%s: range %f != %s", line, p->range_km, val);
    } else if (!strcmp(key, "mice")) {
        CHECK(!strcmp(p->mic_e_message, val), "%s: mic-e '%s' != '%s'", line, p->mic_e_message, val);
    } else if (!strcmp(key, "comment")) {
        CHECK(!strcmp(p->comment, val), "%s: comment '%s' != '%s'", line, p->comment, val);
    } else if (!strcmp(key, "amb")) {
        CHECK(p->pos_ambiguity == (int)v, "%s: amb %d != %s", line, p->pos_ambiguity, val);
    } else if (!strcmp(key, "decode")) {
        /* handled by the caller */
    } else {
        CHECK(0, "%s: unknown vector field %s", line, key);
    }
}

static void run_vectors(const char *path)
{
    FILE *f = fopen(path, "r");
    char row[2048];
    int rows = 0;
    CHECK(f != NULL, "cannot open %s", path);
    if (!f) return;
    while (fgets(row, sizeof(row), f)) {
        char *tab, *line = row, *field;
        aprs_packet p;
        int rc, want_fail = 0;
        size_t n = strlen(row);
        if (n && row[n - 1] == '\n') row[--n] = '\0';
        if (!n || row[0] == '#') continue;
        tab = strchr(row, '\t');
        if (tab) *tab = '\0';
        if (tab && strstr(tab + 1, "decode=fail")) want_fail = 1;
        rc = aprs_decode(line, &p);
        rows++;
        if (want_fail) {
            CHECK(rc != 0, "%s: decoded but expected not TNC2", line);
            continue;
        }
        CHECK(rc == 0, "%s: not decoded", line);
        if (rc != 0) continue;
        field = tab ? tab + 1 : NULL;
        while (field && *field) {
            char *next = strchr(field, '\t');
            char *eq;
            if (next) *next = '\0';
            eq = strchr(field, '=');
            if (eq) {
                *eq = '\0';
                check_field(line, &p, field, eq + 1);
            }
            field = next ? next + 1 : NULL;
        }
    }
    fclose(f);
    CHECK(rows > 40, "only %d vectors", rows);
}

static void test_version(void)
{
    CHECK(aprs_abi() == APRS_ABI, "abi");
    CHECK(strstr(aprs_version(), "contract=aprs-c1") && strstr(aprs_version(), "abi=1"),
          "version %s", aprs_version());
    CHECK(aprs_sizeof(APRS_SIZEOF_PACKET) == sizeof(aprs_packet), "sizeof");
    CHECK(aprs_sizeof(99) == 0, "sizeof unknown");
}

static void test_passcode(void)
{
    /* Well-known values (aprsc/aprsd algorithm): N0CALL 13023. */
    CHECK(aprs_passcode("N0CALL") == 13023, "N0CALL %d", aprs_passcode("N0CALL"));
    CHECK(aprs_passcode("n0call-9") == 13023, "ssid/case");
    CHECK(aprs_passcode("") == -1, "empty");
}

static void test_encoders(void)
{
    char b[256];
    CHECK(aprs_encode_message(b, sizeof(b), "dl1abc", "hello", "7") > 0 &&
          !strcmp(b, ":DL1ABC   :hello{7"), "message %s", b);
    CHECK(aprs_encode_message(b, sizeof(b), "DL1ABC", "bad{x", NULL) == -1, "forbidden {");
    CHECK(aprs_encode_message(b, sizeof(b), "TOOLONGCALL", "x", NULL) == -1, "long call");
    CHECK(aprs_encode_ack(b, sizeof(b), "DL1ABC-0", "12", 0) > 0 && !strcmp(b, ":DL1ABC   :ack12"),
          "ack %s", b);
    CHECK(aprs_encode_ack(b, sizeof(b), "DL1ABC", "12", 1) > 0 && !strcmp(b, ":DL1ABC   :rej12"),
          "rej %s", b);
    CHECK(aprs_encode_bulletin(b, sizeof(b), '1', "wx", "Sturm") > 0 &&
          !strcmp(b, ":BLN1WX   :Sturm"), "bulletin %s", b);
    CHECK(aprs_encode_bulletin(b, sizeof(b), 'A', "WX", "x") == -1, "group needs digit");
    CHECK(aprs_encode_status(b, sizeof(b), "QRV", "092345") > 0 && !strcmp(b, ">092345zQRV"),
          "status %s", b);
    CHECK(aprs_encode_position(b, sizeof(b), 49.058333, -72.029167, '/', '>', "x", NULL, 0) > 0
          && !strcmp(b, "!4903.50N/07201.75W>x"), "position %s", b);
    CHECK(aprs_encode_position(b, sizeof(b), 49.9999999, 8.2, '/', '[', "", "010203", 1) > 0 &&
          !strcmp(b, "@010203z5000.00N/00812.00E["), "position rollover %s", b);
    CHECK(aprs_encode_object(b, sizeof(b), "FIELDDAY", 49.95, 8.25, '/', ';', "Contest",
                             "092345", 0) > 0 &&
          !strcmp(b, ";FIELDDAY *092345z4957.00N/00815.00E;Contest"), "object %s", b);
    CHECK(aprs_encode_object(b, sizeof(b), "A_B", 0, 0, '/', '.', "", "000000", 0) == -1,
          "object name with _");
    CHECK(aprs_encode_item(b, sizeof(b), "AID #2", 49.0583333, -72.0291667, '/', 'A', "", 1) > 0
          && !strcmp(b, ")AID #2_4903.50N/07201.75WA"), "item %s", b);
    CHECK(aprs_encode_message(b, 5, "DL1ABC", "hello", NULL) == -2, "small buffer");
}

static void test_roundtrip(void)
{
    char b[256], line[300];
    aprs_packet p;
    aprs_encode_object(b, sizeof(b), "RT", 50.5, -1.25, '\\', 'n', "c", "010101", 1);
    snprintf(line, sizeof(line), "DG6FL>APZWOW:%s", b);
    CHECK(aprs_decode(line, &p) == 0 && p.type == APRS_T_OBJECT && p.killed &&
          !strcmp(p.name, "RT") && near(p.lat, 50.5, 1e-4) && near(p.lon, -1.25, 1e-4),
          "object round trip %s", line);
}

static void filter_pkt(const char *line, aprs_packet *p)
{
    CHECK(aprs_decode(line, p) == 0, "filter packet %s", line);
}

static void test_filter(void)
{
    char err[128];
    aprs_packet pos, obj, msg, wx, digi, cwop;
    filter_pkt("DL1ABC-9>APRS,DB0XX*,WIDE2-1,qAR,DB0IGT:!4954.00N/00812.00E>", &digi);
    filter_pkt("DL2XYZ>APRS,TCPIP*,qAC,T2:!4954.00N/00830.00E-home", &pos);
    filter_pkt("DB0ABC>APRS,TCPIP*,qAC,T2:;FIELDDAY *092345z4955.00N/00815.00E;x", &obj);
    filter_pkt("DL1>APRS,TCPIP*,qAS,T2::DG6FL    :hi{1", &msg);
    filter_pkt("DB0WX>APRS,TCPIP*:_10090556c220s004t077", &wx);
    filter_pkt("CW1234>APRS,TCPIP*,qAC,T2:@092345z4903.50N/07201.75W_220/004t077", &cwop);
    CHECK(aprs_filter_check("r/49.9/8.2/100 t/poimqstunw -p/CW", err, sizeof(err)) == 0,
          "valid %s", err);
    CHECK(aprs_filter_check("r/49.9/8.2", err, sizeof(err)) == -1, "r needs 3");
    CHECK(aprs_filter_check("x/1", err, sizeof(err)) == -1, "unknown type");
    CHECK(aprs_filter_check("t/pz", err, sizeof(err)) == -1, "unknown t letter");
    CHECK(aprs_filter_check("r/1/2/3\nuser X", err, sizeof(err)) == -1, "control char");
    CHECK(aprs_filter_match("r/49.9/8.2/30", &pos, NAN, NAN) == 1, "range in");
    CHECK(aprs_filter_match("r/49.9/8.2/5", &pos, NAN, NAN) == 0, "range out");
    CHECK(aprs_filter_match("a/50/8/49/9", &pos, NAN, NAN) == 1, "area");
    CHECK(aprs_filter_match("m/30", &pos, 49.9, 8.2) == 1, "my range");
    CHECK(aprs_filter_match("m/30", &pos, NAN, NAN) == 0, "my range without position");
    CHECK(aprs_filter_match("p/DL", &pos, NAN, NAN) == 1, "prefix");
    CHECK(aprs_filter_match("b/DL2*", &pos, NAN, NAN) == 1, "budlist wildcard");
    CHECK(aprs_filter_match("b/DL2", &pos, NAN, NAN) == 0, "budlist exact");
    CHECK(aprs_filter_match("o/FIELD*", &obj, NAN, NAN) == 1, "object");
    CHECK(aprs_filter_match("t/o", &obj, NAN, NAN) == 1 && aprs_filter_match("t/p", &obj, NAN, NAN) == 0,
          "type object");
    CHECK(aprs_filter_match("t/m", &msg, NAN, NAN) == 1, "type message");
    CHECK(aprs_filter_match("g/DG6FL", &msg, NAN, NAN) == 1, "group/addressee");
    CHECK(aprs_filter_match("t/w", &wx, NAN, NAN) == 1, "type weather");
    CHECK(aprs_filter_match("t/w", &cwop, NAN, NAN) == 0 && aprs_filter_match("t/c", &cwop, NAN, NAN) == 1,
          "cwop");
    CHECK(aprs_filter_match("d/DB0XX", &digi, NAN, NAN) == 1, "digi used");
    CHECK(aprs_filter_match("d/WIDE2*", &digi, NAN, NAN) == 0, "digi not used");
    CHECK(aprs_filter_match("e/DB0IGT", &digi, NAN, NAN) == 1, "entry");
    CHECK(aprs_filter_match("q/R", &digi, NAN, NAN) == 1 && aprs_filter_match("q/C", &digi, NAN, NAN) == 0,
          "q construct");
    CHECK(aprs_filter_match("u/APRS", &pos, NAN, NAN) == 1, "unproto");
    CHECK(aprs_filter_match("s/>", &digi, NAN, NAN) == 1 && aprs_filter_match("s/-", &digi, NAN, NAN) == 0,
          "symbol");
    CHECK(aprs_filter_match("p/DL -b/DL2XYZ", &pos, NAN, NAN) == 0, "exclusion");
    CHECK(aprs_filter_match("f/DL1ABC/50", &pos, NAN, NAN) == 0, "friend never matches");
    CHECK(aprs_filter_match("", &pos, NAN, NAN) == 0, "empty passes nothing");
}

/* Random and truncated input never crashes or reads out of bounds (run under sanitize). */
static void test_garbage(void)
{
    static const char *const seeds[] = {
        "N0CALL>S32UVT:`(_fn\"Oj/]\"4L}", "DB0ABC>APRS:;FIELDDAY *092345z/5L!!<*e7>7P[",
        "DB0WX>APRS:@092345z4903.50N/07201.75W_220/004g005t077r000p000P000h50b09900",
        "DL1>APRS::DG6FL    :reply ack{AB}12", "DL1>APRS:$GPRMC,1,A,4807.038,N,01131.000,E,1,2",
        "I>APRS,TCPIP*:}DL2>APRS:}DL3>APRS:!4903.50N/07201.75W-", "DB0T>APRS:T#005,1,2,3,4,5,1"};
    unsigned int seed = 12345;
    size_t i, k;
    aprs_packet p;
    char buf[600];
    for (i = 0; i < sizeof(seeds) / sizeof(seeds[0]); i++) {
        size_t n = strlen(seeds[i]);
        for (k = 0; k <= n; k++) {          /* every truncation */
            memcpy(buf, seeds[i], k);
            buf[k] = '\0';
            aprs_decode(buf, &p);
        }
        for (k = 0; k < 2000; k++) {        /* random byte flips */
            size_t j;
            memcpy(buf, seeds[i], n + 1);
            for (j = 0; j < 3; j++) {
                seed = seed * 1103515245u + 12345u;
                buf[(seed >> 8) % n] = (char)(1 + (seed >> 16) % 255);
            }
            aprs_decode(buf, &p);
            aprs_filter_match("r/49/8/100 t/poimqstunw b/DL* o/F* d/DB0* e/T2 s/>/ -p/CW", &p,
                              49.0, 8.0);
        }
    }
    CHECK(1, "garbage");
}

int main(int argc, char **argv)
{
    test_version();
    test_passcode();
    test_encoders();
    test_roundtrip();
    test_filter();
    test_garbage();
    if (argc > 1) run_vectors(argv[1]);
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
