# Source SBOM

Source repository inventory only; completeness is UNKNOWN. Syft inventories tracked source/package manifests. Gitlinks and declared vendored/model evidence are inspected at immutable pins. CMake declarations are conditional requirements, not resolved installations. This is NOT proof of operating-system packages, static-link closure, build-time resolution, or the contents of release binaries.

Generation: **success**

## Source identity

wave-owl/aprs-core `94eb3f6a71e8ca755355ae727f85f973b3c56fe4`

- sbom:role: source-root
- sbom:git-commit: 94eb3f6a71e8ca755355ae727f85f973b3c56fe4
- sbom:git-tree: 4794f4f077837ed650950bc02575dba5d08255c7
- sbom:source-repository: https://github.com/wave-owl/aprs-core
- sbom:release-tags: ["v0.1.0"]
- sbom:license-evidence: [{"path": "LICENSE", "sha256": "3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986"}]
- sbom:license-resolution: unresolved; manual review required

## Generation errors

None.

## Components and unresolved licenses

| Component | Version | Role | License |
| --- | --- | --- | --- |
| wave-owl/aprs-core | 94eb3f6a71e8ca755355ae727f85f973b3c56fe4 | source-root | unresolved; manual review required |
| wave-owl/aprs-python | c2a0f18ce028a4cced582567a73d57f0d03cd00f | reference-only | unresolved; manual review required |
| wave-owl/aprsc | 53782729641ff38ec1c338122b59b537774e82e9 | reference-only | unresolved; manual review required |
| wave-owl/libfap | 074f4ab7732a9e37c1dc07d10bf3048e294bce0b | reference-only | unresolved; manual review required |
| src/aprs_decode.c | 074f4ab7732a9e37c1dc07d10bf3048e294bce0b | declared-vendored-source | unresolved; manual review required |
| src/aprs_filter.c | 53782729641ff38ec1c338122b59b537774e82e9 | declared-vendored-source | unresolved; manual review required |
| actions/checkout | v4 | Syft source/package discovery | unresolved |
| actions/setup-python | v5 | Syft source/package discovery | unresolved |
| /Users/gerolfziegenhain/src/wave-owl/local/sbom-core-preflight-v2/aprs-core/.source-input/.github/workflows/ci.yml | unresolved | Syft source/package discovery | unresolved |

License evidence paths/hashes, source manifests, model hashes and conditional
CMake declarations are recorded in SBOM.cdx.json. Declared upstream pins do not
prove vendored equivalence. No dependency installation or build was performed.
