# P2 scope: provisioning boundary and regression hardening

P2 starts only after P1 is merged. Its goal is to reduce the risk and size of `manual_config.c` by extracting testable pure logic first, while preserving all current USB NCM, provisioning, Wi-Fi, storage, and HTTP behavior for valid requests.

Current delivery status:

- P2.1 is merged: dependency-free form/profile policy with strict host tests;
- P2.2 is merged: provisioning HTTP/form/JSON boundary extracted from `manual_config.c`;
- P2.3 adds explicit resource, reproducibility, dependency, and CI diagnosis gates;
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

P2.3 turns the known-good P2.2 build into explicit regression budgets and makes frontend generation reproducible from the committed lockfile.

### Firmware resource budget

The baseline comes from the green P2.2 CI build using PlatformIO 6.1.19, `espressif32 @ 6.13.0`, and ESP-IDF 5.5.3:

| Resource | P2.2 baseline | Explicit headroom | Blocking limit | Reported total |
| --- | ---: | ---: | ---: | ---: |
| Static RAM | 46,860 B | 8,192 B | 55,052 B | 327,680 B |
| Flash | 1,140,916 B | 32,768 B | 1,173,684 B | 1,572,864 B |

The previous green P2.1 build used 46,860 B RAM and 1,140,252 B flash, so the P2.2 HTTP-boundary extraction changed the linked image by 0 B RAM and +664 B flash before the budget was frozen.

`scripts/firmware_size_budget.json` stores the baseline, headroom, limits, and expected totals. `scripts/check_firmware_size.py` runs PlatformIO's size target, parses the actual report, verifies that the board/partition totals have not silently changed, and fails when either absolute limit is exceeded.

The PlatformIO RAM number is the linked/static RAM footprint. It is not a runtime heap watermark. Runtime `Free heap` and `Min free heap` diagnostics remain necessary for dynamic-memory regressions.

The limits are intentionally absolute rather than percentage based. Updating them requires a reviewed known-good build and an explicit explanation of why additional headroom is needed.

### Reproducible frontend assets

`web/package-lock.json` is mandatory. `scripts/build_web.py` no longer falls back to `npm install`; a clean firmware build installs frontend dependencies with `npm ci`.

`scripts/check_web_reproducible.sh`:

1. requires the committed lockfile;
2. builds the frontend and captures generated `src/web_assets.h`;
3. removes generated output;
4. builds the same sources again with the same installed lockfile dependency set;
5. compares the two generated headers byte for byte.

`src/web_assets.h` remains ignored because it is generated build output. Reproducibility is therefore checked by regeneration rather than by comparing an ignored file with Git.

### Dependency audit policy

The green P2.2 CI run reported three npm audit findings: one low and two high. P2.3 does not hide those findings or apply an unreviewed `npm audit fix`.

CI runs `npm audit --audit-level=critical`. A critical advisory is blocking immediately; lower-severity findings remain visible and must be handled through reviewed dependency updates that keep the UI/build regression checks green. This avoids turning an existing reviewed baseline into a permanently red pipeline while still preventing new critical exposure from passing silently.

### CI diagnosis

The previous aggregate static step is split into visible gates:

- firmware build;
- firmware resource budget;
- strict host policy tests;
- web reproducibility and dependency audit;
- C static analysis;
- documentation lint.

`scripts/quality.sh --static` remains available as the aggregate local compatibility command, while the individual modes let CI and developers run one failing gate directly.

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
