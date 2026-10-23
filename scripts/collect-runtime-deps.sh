#!/usr/bin/env bash
# Print Debian/Ubuntu runtime packages for the trusted CI-built binaries.
# ldd includes transitive dependencies; fail rather than ship a partial list.
set -euo pipefail
export LC_ALL=C

if (( $# == 0 )); then
    echo "Usage: $0 BINARY [BINARY ...]" >&2
    exit 2
fi

packages=()
for binary in "$@"; do
    libraries=$(ldd "$binary")
    if [[ "$libraries" == *"=> not found"* ]]; then
        echo "Unresolved runtime dependency of $binary:" >&2
        echo "$libraries" >&2
        exit 1
    fi
    mapfile -t paths < <(awk '$2 == "=>" && $3 ~ /^\// {print $3} $1 ~ /^\// {print $1}' <<< "$libraries")
    if (( ${#paths[@]} == 0 )); then
        echo "No runtime libraries found for $binary" >&2
        exit 1
    fi
    for library in "${paths[@]}"; do
        # dpkg's recorded path may predate the /usr merge. Try both spellings
        # and the resolved path, keeping the architecture qualifier intact.
        owner=
        for path in "$library" "$(readlink -f "$library")" "/usr${library}" "${library#/usr}"; do
            if owner=$(dpkg-query -S "$path" 2>/dev/null |
                awk '/^[a-z0-9][a-z0-9+.-]*(:[a-z0-9-]+)?: \// {print; exit}'); then
                [[ -n "$owner" ]] || continue
                break
            fi
        done
        if [[ -z "$owner" || "$owner" != *": /"* ]]; then
            echo "No distribution package owns $library" >&2
            exit 1
        fi
        packages+=("${owner%%: /*}")
    done
done
printf '%s\n' "${packages[@]}" | sort -u
