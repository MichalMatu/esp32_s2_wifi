# P1 quality refactor

This change intentionally keeps the ESP-IDF/NVS persistence layer in `config_access.c` and moves only pure access-mode policy into `config_access_policy.c`.

The extracted policy covers:

- validation of supported access modes,
- parsing of `local` and `captive` names,
- gateway and DNS policy,
- user-facing names, labels, and host descriptions.

The module has no ESP-IDF dependency and is covered by a host-side C test compiled with strict warnings. The existing firmware build remains the integration gate for the ESP32-S2 target.

This is deliberately narrower than a wholesale split of `manual_config.c`: provisioning HTTP state, Wi-Fi lifecycle, scan state, and NVS behavior remain unchanged in this P1 refactor.
