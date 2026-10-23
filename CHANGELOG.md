# Changelog

## 0.1.0 (2026-10-23)

- Decoder after libfap 1.5 without PCRE: positions (uncompressed, compressed, Mic-E, NMEA,
  ambiguity), objects, items, waypoints, weather, telemetry, messages, bulletins, status,
  third party.
- Encoders: position, message, ack/rej, bulletin (group), status, object, item.
- APRS-IS passcode and server-side filter after aprsc.
- Oracle vectors from aprslib (49 corpus lines), overrides with reasons; sanitizer and
  garbage/truncation tests. Contract aprs-c1, ABI 1, tags enc=1 filter=1.
