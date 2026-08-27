#!/usr/bin/env sh
set -eu

compile_and_run_host_test() {
    cc_bin="$1"
    test_name="$2"
    test_source="$3"
    policy_source="$4"

    test_bin="$(mktemp "${TMPDIR:-/tmp}/esp32-s2-${test_name}.XXXXXX")"
    trap 'rm -f "$test_bin"' EXIT HUP INT TERM

    "$cc_bin" \
        -std=c11 \
        -Wall \
        -Wextra \
        -Werror \
        -pedantic \
        -Isrc \
        "$test_source" \
        "$policy_source" \
        -o "$test_bin"
    "$test_bin"
    rm -f "$test_bin"
    trap - EXIT HUP INT TERM
}

run_host_tests() {
    cc_bin="${CC:-cc}"
    if ! command -v "$cc_bin" >/dev/null 2>&1; then
        echo "C compiler not installed: $cc_bin"
        exit 1
    fi

    compile_and_run_host_test "$cc_bin" config-policy \
        tests/config_access_policy_test.c src/config_access_policy.c
    compile_and_run_host_test "$cc_bin" form-profile-policy \
        tests/form_profile_policy_test.c src/form_profile_policy.c
    python3 tests/check_firmware_size_test.py
}

run_cppcheck() {
    if command -v cppcheck >/dev/null 2>&1; then
        cppcheck_bin="cppcheck"
    elif [ -x "$HOME/.platformio/packages/tool-cppcheck/cppcheck" ]; then
        cppcheck_bin="$HOME/.platformio/packages/tool-cppcheck/cppcheck"
    else
        echo "cppcheck not installed"
        exit 1
    fi

    "$cppcheck_bin" \
        --enable=warning,style,performance,portability \
        --inline-suppr \
        --suppress=missingIncludeSystem \
        --suppress=constParameterPointer:src/usb_ncm_iface.c \
        --suppress=constParameterCallback:src/usb_ncm_iface.c \
        --std=c11 \
        --error-exitcode=1 \
        src
}

run_markdownlint() {
    if command -v markdownlint-cli2 >/dev/null 2>&1; then
        markdownlint-cli2 README.md docs/P1_QUALITY_NOTES.md docs/P2_SCOPE.md
    else
        echo "markdownlint-cli2 not installed; skipping markdown lint"
    fi
}

run_web_checks() {
    if [ ! -d web ]; then
        return
    fi

    if [ ! -f web/package-lock.json ]; then
        echo "web/package-lock.json is required" >&2
        exit 1
    fi

    (cd web && npm ci)
    (cd web && npm run test)
    (cd web && npm audit --audit-level=critical)
    sh scripts/check_web_reproducible.sh --skip-install
}

run_build_and_budget() {
    report_path=".pio/firmware-build.log"
    mkdir -p .pio

    if pio run >"$report_path" 2>&1; then
        cat "$report_path"
    else
        status=$?
        cat "$report_path"
        return "$status"
    fi

    python scripts/check_firmware_size.py --report "$report_path"
}

run_static_checks() {
    run_host_tests
    run_web_checks
    run_cppcheck
    run_markdownlint
}

case "${1:-all}" in
    --host-tests|host-tests)
        run_host_tests
        ;;
    --web|web)
        run_web_checks
        ;;
    --cppcheck|cppcheck)
        run_cppcheck
        ;;
    --markdown|markdown)
        run_markdownlint
        ;;
    --static|static)
        run_static_checks
        ;;
    --build|build)
        run_build_and_budget
        ;;
    all)
        if command -v pre-commit >/dev/null 2>&1; then
            pre-commit run --all-files
        else
            echo "pre-commit not installed; skipping hook checks"
        fi

        run_host_tests
        run_web_checks
        run_build_and_budget
        run_cppcheck
        run_markdownlint
        ;;
    *)
        echo "usage: sh scripts/quality.sh [--host-tests|--web|--cppcheck|--markdown|--static|--build]"
        exit 2
        ;;
esac
