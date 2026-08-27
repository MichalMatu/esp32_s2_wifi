#!/usr/bin/env sh
set -eu

run_host_tests() {
    cc_bin="${CC:-cc}"
    if ! command -v "$cc_bin" >/dev/null 2>&1; then
        echo "C compiler not installed: $cc_bin"
        exit 1
    fi

    test_bin="$(mktemp "${TMPDIR:-/tmp}/esp32-s2-config-policy.XXXXXX")"
    trap 'rm -f "$test_bin"' EXIT HUP INT TERM

    "$cc_bin" \
        -std=c11 \
        -Wall \
        -Wextra \
        -Werror \
        -pedantic \
        -Isrc \
        tests/config_access_policy_test.c \
        src/config_access_policy.c \
        -o "$test_bin"
    "$test_bin"
    rm -f "$test_bin"
    trap - EXIT HUP INT TERM
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
        markdownlint-cli2 README.md
    else
        echo "markdownlint-cli2 not installed; skipping markdown lint"
    fi
}

run_web_checks() {
    if [ ! -d web ]; then
        return
    fi

    if [ -f web/package-lock.json ]; then
        (cd web && npm ci)
    else
        (cd web && npm install)
    fi

    (cd web && npm run check && npm run test && npm run build)
}

case "${1:-all}" in
    --static|static)
        run_host_tests
        run_web_checks
        run_cppcheck
        run_markdownlint
        ;;
    --build|build)
        pio run
        ;;
    all)
        if command -v pre-commit >/dev/null 2>&1; then
            pre-commit run --all-files
        else
            echo "pre-commit not installed; skipping hook checks"
        fi

        run_host_tests
        run_web_checks
        pio run
        run_cppcheck
        run_markdownlint
        ;;
    *)
        echo "usage: sh scripts/quality.sh [--static|--build]"
        exit 2
        ;;
esac
