#!/usr/bin/env sh
set -eu

root_dir="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
web_dir="$root_dir/web"
header_path="$root_dir/src/web_assets.h"

if [ ! -f "$web_dir/package-lock.json" ]; then
    echo "web/package-lock.json is required for reproducible web assets" >&2
    exit 1
fi

skip_install=false
case "${1:-}" in
    "") ;;
    --skip-install) skip_install=true ;;
    *)
        echo "usage: sh scripts/check_web_reproducible.sh [--skip-install]" >&2
        exit 2
        ;;
esac

if [ "$skip_install" = false ]; then
    (cd "$web_dir" && npm ci)
fi

first_header="$(mktemp "${TMPDIR:-/tmp}/esp32-s2-web-assets.XXXXXX")"
cleanup() {
    rm -f "$first_header"
}
trap cleanup EXIT HUP INT TERM

rm -rf "$web_dir/dist"
rm -f "$header_path"
(cd "$web_dir" && npm run build)

if [ ! -s "$header_path" ]; then
    echo "first web build did not generate src/web_assets.h" >&2
    exit 1
fi
cp "$header_path" "$first_header"
first_size="$(wc -c < "$first_header" | tr -d ' ')"

rm -rf "$web_dir/dist"
rm -f "$header_path"
(cd "$web_dir" && npm run build)

if [ ! -s "$header_path" ]; then
    echo "second web build did not generate src/web_assets.h" >&2
    exit 1
fi

if ! cmp -s "$first_header" "$header_path"; then
    echo "web asset generation is not reproducible: consecutive headers differ" >&2
    exit 1
fi

second_size="$(wc -c < "$header_path" | tr -d ' ')"
echo "web assets reproducible: $second_size bytes (first build: $first_size bytes)"
