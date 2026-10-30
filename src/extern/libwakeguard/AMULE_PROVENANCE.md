# libwakeguard vendoring provenance

This is a verbatim partial snapshot of the standalone libwakeguard repository.
Update the dependency upstream; do not edit these source files in aMule.

| | |
| --- | --- |
| Upstream | <https://github.com/3togo/libwakeguard> |
| Commit | `8a55bcb21e6d0eafb4fcdc4aeed1bbcf5317a810` |
| Version | 0.2.0 |
| License | MIT — see `LICENSE`, `NOTICE.md` and `third_party/keepawake-rs.LICENSE` |
| Local patches | none |

`SHA256SUMS` covers every upstream file selected for this snapshot. The library
sources, public headers, build/package configuration, own formatting rules and
licence/provenance notices are kept. Standalone tests, examples and CI workflows
are omitted; the aMule CMake adapter pins their build switches off. The tests run
in the standalone repository. The separate aMule tests exercise the production
adapter with injected backends and real library calls over an isolated D-Bus.

All platform sources are kept, including Windows/macOS and the unsupported-build
fallback. aMule links a static library; it does not install the standalone
library, headers or CMake package. Its licences are installed separately by
`cmake/wakeguard.cmake` and reproduced in `docs/THIRDPARTY.md`.

To verify after checking out the upstream commit:

```sh
cd src/extern/libwakeguard
sha256sum -c SHA256SUMS
# For each listed upstream path, compare against the checked-out upstream source.
```

Update the pin, selected files, checksums and version together. The two files
`AMULE_PROVENANCE.md` and `SHA256SUMS` are aMule metadata, not upstream source.
