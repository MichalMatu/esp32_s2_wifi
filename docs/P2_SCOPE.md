# P2 scope: provisioning boundary and regression hardening

P2 starts only after P1 is merged. Its goal is to reduce the risk and size of `manual_config.c` by extracting testable pure logic first, while preserving all current USB NCM, provisioning, Wi-Fi, storage, and HTTP behavior for valid requests.

Current delivery status:

- P2.1 is merged: dependency-free form/profile policy with strict host tests;
- P2.2 is implemented in a dedicated HTTP-boundary PR;
- P2.3 remains intentionally separate follow-up work;
- malformed form input is handled strictly: encoded NUL (`%00`) is rejected because it cannot be represented safely in the C-string output without hiding a trailing suffix.

## P2.1 - Form and profile policy extraction

Extract dependency-free helpers from `manual_config.c` into a small C module suitable for host tests.

Target responsibilities:

- hexadecimal digit decoding;
- URL/form component decoding;
- form key lookup;
- bounded profile-index parsing;
- bounded profile ID formatting/presentation.

Required host tests:

- `+` to space decoding;
- valid `%xx` decoding with upper- and lower-case hex;
- malformed and incomplete percent escapes;
- encoded NUL rejection;
- empty values and missing keys;
- output-buffer exhaustion;
- exact key matching and duplicate/adjacent fields;
- valid profile indexes `0..WIFI_PROFILE_MAX-1`;
- negative, signed, non-numeric, overflow, and out-of-range indexes;
- bounded profile-index formatting and undersized output buffers.

Acceptance criterion: remove the source-local `-Wno-error=format-truncation` exception from `manual_config.c` because the profile presentation helper has an explicit tested bound.

P2.1 implementation uses `form_profile_policy.c/.h`. Public operations cover value extraction, bounded profile-index parsing, and bounded profile-index formatting. Hex and percent decoding remain private implementation details and are exercised through the public value-extraction contract.

## P2.2 - Thin HTTP handler boundary

Move HTTP/form presentation responsibilities out of `manual_config.c` without changing endpoint paths or response contracts.

P2.2 implementation uses `provisioning_http.c/.h` for:

- JSON string escaping and typed field serialization;
- common action JSON responses;
- profile-list and profile-error response presentation;
- bounded HTTP form-body reads;
- config/LED form value extraction;
- Wi-Fi credential extraction;
- bounded profile-index extraction.

The boundary intentionally leaves these responsibilities in `manual_config.c`:

- Wi-Fi connection lifecycle and validation task state;
- scan lifecycle, snapshots, sorting, and Wi-Fi API calls;
- NVS/profile persistence calls;
- endpoint registration and runtime orchestration;
- coredump, mDNS, and USB-facing provisioning runtime behavior.

This keeps P2.2 architectural rather than turning it into a Wi-Fi or USB state-machine rewrite. Existing endpoint paths and JSON schemas remain unchanged.

## P2.3 - Regression and resource gates

Extend CI from functional compilation to explicit regression budgets.

Add:

- a firmware size check with documented flash/RAM headroom rather than an arbitrary percentage;
- a repeatable command that records or validates PlatformIO size output;
- dependency hygiene for the frontend lockfile, including review of current npm audit findings before making them blocking;
- a check that generated web assets are reproducible from the committed frontend sources and lockfile;
- host-test execution as a separately visible CI step if that improves failure diagnosis.

Any budget must be derived from a known-good build and leave intentional headroom for ESP-IDF/toolchain variation.

## P2 non-goals

P2 does not include:

- migration to ESP-IDF 6;
- redesign of USB NCM behavior or macOS interoperability;
- Wi-Fi provisioning UX redesign;
- endpoint renaming or API schema changes;
- NVS schema migration;
- captive-portal behavior changes;
- broad replacement of the Wi-Fi connection state machine;
- unrelated frontend feature work.

## P2 completion criteria

P2 is complete when:

1. the selected form/profile logic is in dependency-free modules with strict host tests;
2. `manual_config.c` is measurably smaller and contains less pure parsing/presentation logic;
3. the `format-truncation` compiler exception is removed;
4. existing endpoint and valid config-mode behavior remains unchanged;
5. the full ESP32-S2 firmware build, host tests, frontend checks, cppcheck, and markdownlint are green;
6. firmware size/resource regression checks are documented and enforced;
7. README and architecture/quality notes describe the new module boundaries.

P2 should be delivered as small reviewable commits (or separate PRs for P2.1, P2.2, and P2.3) rather than as one wholesale rewrite of `manual_config.c`.
