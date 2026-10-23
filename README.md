# aprs-core

APRS packet decoder and encoder, the APRS-IS passcode and the APRS-IS server-side filter
matcher in C99, no dependencies besides libm. Native core of
[Wave Owl](https://github.com/wave-owl) (`src/aprs_native.py`).

- **Decoder** (`aprs_decode`): uncompressed and compressed positions with and without
  timestamp and with position ambiguity, course/speed, PHG, RNG, altitude, DAO stripped;
  Mic-E (position, speed, course, message bits, altitude); objects (live/killed), items, NMEA
  `$GPWPL`/`$GPRMC`/`$GPGGA`; weather in positions and positionless (SI units); telemetry and
  its PARM/UNIT/EQNS/BITS messages; messages with number and reply-ack, acks, rejects,
  bulletins, announcements, group and NWS bulletins; status; queries, capabilities,
  user-defined; third-party traffic unwrapped. The structure follows
  [libfap](https://github.com/wave-owl/libfap) 1.5 (commit `074f4ab`, GPL-1.0-or-later or
  Artistic), the C port of Ham::APRS::FAP, without its PCRE dependency.
- **Encoders**: position, message, ack/rej, bulletin (incl. group), status, object, item;
  invalid input is refused, nothing malformed is written.
- **APRS-IS**: passcode (`aprs_passcode`, from [aprsc](https://github.com/wave-owl/aprsc)
  `src/passcode.c`, BSD) and the server-side filter (`aprs_filter_check`,
  `aprs_filter_match`: r a m f p b o u g e d q s t and `-` exclusions, after aprsc
  `src/filter.c`).
- **Oracle**: [aprslib](https://github.com/wave-owl/aprs-python) (commit `c2a0f18`,
  GPL-2.0-or-later) is run by `tools/gen_vectors.py` on `tests/vectors/corpus.txt`; its
  answers, plus `tests/vectors/overrides.tsv` where this core differs on purpose (with the
  reason) or aprslib has no answer, are `tests/vectors/decode.tsv`. No aprslib code is used.

Contract `aprs-c1`, ABI 1, capability tags `enc=1 filter=1`
(`aprs-core 0.1.0 contract=aprs-c1 abi=1 enc=1 filter=1`). `aprs_sizeof(0)` gives the size of
`aprs_packet` for bindings.

## Build and test

```sh
git clone --recurse-submodules https://github.com/wave-owl/aprs-core
make            # build/libaprs.a and build/libaprs.{so,dylib}
make test       # API, passcode, encoders, round trip, filter, garbage/truncation, vectors
make sanitize   # the tests under ASan + UBSan
make ref        # regenerate tests/vectors/decode.tsv with aprslib (Python 3)
```

The references are pinned submodules under `ref/` (mirrors in the wave-owl organisation).

## Licence

GPL-3.0-or-later (LICENSE). libfap's licence allows "any later version"; aprsc is BSD; see
NOTICE.
