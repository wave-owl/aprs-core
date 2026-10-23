/* aprs-core internal declarations. See include/aprs.h for sources and licence. */
#ifndef APRS_INTERNAL_H
#define APRS_INTERNAL_H

#include "aprs.h"

/* Formats a latitude "DDMM.mmN" (9 bytes incl. NUL) / longitude "DDDMM.mmE" (10 bytes). */
int aprs_fmt_lat(char *out, double lat);
int aprs_fmt_lon(char *out, double lon);

#endif
