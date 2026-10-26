#!/usr/bin/env bash
# Copyright (c) 2026 aMule Team
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

# Run as root on the disposable Ubuntu CI runner. Bound network work separately
# from dpkg: an active but very slow mirror never trips apt's connection timeout.
repo_root=$(cd "$(dirname "$0")/.." && pwd)
cp "$repo_root/.github/apt-ci.conf" /etc/apt/apt.conf.d/99-ci

download() {
    timeout --kill-after=10s 60s apt-get update &&
        timeout --kill-after=10s 240s apt-get install --download-only -y "$@"
}

if ! download "$@"; then
    echo '::warning::Dependency download failed or stalled; retrying through archive.ubuntu.com.'
    # Hosted runners use mirror+file sources; also support ordinary list and
    # deb822 sources. Keep security suites, third-party sources and apt's cache.
    python3 - <<'PY'
from pathlib import Path
root = Path('/etc/apt')
paths = [root / 'sources.list', *root.glob('apt-mirrors*.txt'),
         *root.glob('sources.list.d/*.list'), *root.glob('sources.list.d/*.sources')]
for path in paths:
    if path.is_file():
        original = path.read_text()
        updated = original.replace('azure.archive.ubuntu.com', 'archive.ubuntu.com')
        if updated != original:
            path.write_text(updated)
PY
    download "$@"
fi

# Never start dpkg until every archive is local. Do not interrupt dpkg with the
# short download deadline or silently return to a slow mirror while installing.
apt-get install --no-download -y "$@"
