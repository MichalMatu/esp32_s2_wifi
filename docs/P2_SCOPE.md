# P2 scope: provisioning boundary and regression hardening

P2 starts only after P1 is merged. Its goal is to reduce the risk and size of `manual_config.c` by extracting testable pure logic first, while preserving all current USB NCM, provisioning, Wi-Fi, storage, and HTTP behavior.

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
- empty values and missing keys;
- output-buffer exhaustion;
- exact key matching and duplicate/adjacent fields;
- valid profile indexes `0..WIFI_PROFILE_MAX-1`;
- negative, non-numeric, overflow, and out-of-range indexes.

Acceptance criterion: remove the source-local `-Wno-error=format-truncation` exception from `manual_config.c` because the profile presentation helper has an explicit tested bound.

## P2.2 - Thin HTTP handler boundary

Move HTTP/form presentation responsibilities out of `manual_config.c` without changing endpoint paths or response contracts.

Preferred boundary:

- pure parsing/formatting helpers remain ESP-IDF-independent;
- HTTP handlers own only request/response orchestration;
- Wi-Fi lifecycle and connection state stay in their existing runtime layer until a separate change has tests for them;
- NVS profile persistence stays in `wifi_profiles.c`.

Candidate extraction targets:

- common action JSON responses;
- profile-list response formatting;
- form credential parsing;
- config-mode form parsing.

Do not combine this with a USB NCM or Wi-Fi state-machine rewrite.

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
4. existing endpoint and config-mode behavior remains unchanged;
5. the full ESP32-S2 firmware build, host tests, frontend checks, cppcheck, and markdownlint are green;
6. firmware size/resource regression checks are documented and enforced;
7. README and architecture/quality notes describe the new module boundaries.

P2 should be delivered as small reviewable commits (or separate PRs for P2.1, P2.2, and P2.3) rather than as one wholesale rewrite of `manual_config.c`.
