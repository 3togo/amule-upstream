#!/bin/sh
# Pinned, reduced source builds for the fully static Alpine bundle.
set -eu
prefix=${1:-/usr/local}
work=${2:-/tmp/amule-media-deps}
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mkdir -p "$work"
python3 - "$script_dir/pins.json" "$work" <<'PY'
import hashlib, json, pathlib, subprocess, sys, urllib.request
work=pathlib.Path(sys.argv[2])
for name, item in json.load(open(sys.argv[1])).items():
    path=work/item['archive']
    if not path.exists(): path.write_bytes(urllib.request.urlopen(item['url']).read())
    if hashlib.sha256(path.read_bytes()).hexdigest()!=item['sha256']:
        raise SystemExit('Checksum mismatch: '+name)
    target=work/name
    target.mkdir(exist_ok=True)
    subprocess.run(['tar','-xf',str(path),'-C',str(target),'--strip-components=1'],check=True)
PY
for project in zenlib mediainfo; do
  cmake -S "$work/$project/Project/CMake" -B "$work/$project-build" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DCMAKE_PREFIX_PATH="$prefix" -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_ZENLIB=OFF -DBUILD_ZLIB=OFF -DCMAKE_DISABLE_FIND_PACKAGE_CURL=ON
  cmake --build "$work/$project-build" -j2
  cmake --install "$work/$project-build"
done
cd "$work/ffmpeg"
./configure --prefix="$prefix" --disable-shared --enable-static --enable-pic \
  --disable-programs --disable-doc --disable-debug --disable-autodetect \
  --disable-network --disable-protocols --disable-encoders --disable-muxers \
  --disable-avdevice --disable-avfilter --disable-swscale --disable-swresample \
  --disable-x86asm
make -j2
make install
mkdir -p "$prefix/share/licenses/amule-media-probe"
cp COPYING.LGPLv2.1 "$prefix/share/licenses/amule-media-probe/FFmpeg-LGPL-2.1.txt"
cp "$work/mediainfo/License.html" "$prefix/share/licenses/amule-media-probe/MediaInfoLib.html"
cp "$work/zenlib/License.txt" "$prefix/share/licenses/amule-media-probe/ZenLib.txt"

cp "$script_dir"/licenses/* "$prefix/share/licenses/amule-media-probe/"
