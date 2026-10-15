#!/usr/bin/env python3
"""Compare ffprobe, libav* and MediaInfoLib without importing aMule or Python packages."""
import argparse
import copy
import ctypes as C
import ctypes.util
from decimal import Decimal, InvalidOperation, ROUND_HALF_UP
import json
import os
from pathlib import Path
from duration import scan
import subprocess
import sys
import time

FIELDS = ('length_seconds', 'bitrate_kbps', 'codec', 'artist', 'album', 'title')
ENTRIES = ('format=format_name,duration,bit_rate:format_tags=artist,title,album:'
           'stream=codec_name,codec_type:stream_disposition=attached_pic:'
           'stream_tags=artist,title,album')


def number(value, divisor=1, rounded=False):
    try:
        n = Decimal(str(value)) / divisor
        if not n.is_finite() or n < 0:
            return 0
        return min(0xffffffff, int(n.to_integral_value(rounding=ROUND_HALF_UP) if rounded else n))
    except (InvalidOperation, ValueError, TypeError):
        return 0


def clean(value):
    value = str(value or '')
    return ''.join(c for c in value if c == '\t' or
                   (ord(c) >= 32 and not 127 <= ord(c) <= 159))[:256].strip()


def normalize(raw, file_size=None, policy='legacy'):
    """Mirror src/MediaProbe.cpp's valid-input selection and normalization rules."""
    fmt = raw.get('format', {})
    streams = [s for s in raw.get('streams', []) if s.get('codec_name') and
               not s.get('disposition', {}).get('attached_pic', 0)]
    videos = [s for s in streams if s.get('codec_type') == 'video']
    audios = [s for s in streams if s.get('codec_type') == 'audio']
    selected = (videos or audios or [{}])[0]
    result = dict(length_seconds=number(fmt.get('duration'), rounded=True),
                  bitrate_kbps=number(fmt.get('bit_rate'), 1000),
                  codec=selected.get('codec_name', ''))
    if policy == 'file-average':
        # Use unrounded duration; using published integer seconds distorts short files.
        try:
            duration = Decimal(str(fmt.get('duration')))
            result['bitrate_kbps'] = number(Decimal(file_size) * 8 / duration, 1000) if duration.is_finite() and duration > 0 else 0
        except (InvalidOperation, TypeError, ValueError):
            result['bitrate_kbps'] = 0
    tags = {k.lower(): clean(v) for k, v in fmt.get('tags', {}).items()}
    fallback = {}
    if not videos and len(audios) == 1 and audios[0]['codec_name'] in ('vorbis', 'opus', 'flac', 'speex'):
        fallback = {k.lower(): clean(v) for k, v in audios[0].get('tags', {}).items()}
    for key in ('artist', 'album', 'title'):
        result[key] = tags.get(key) or fallback.get(key, '')
    result['outcome'] = 'Extracted' if result['length_seconds'] or result['codec'] else 'NoUsableMetadata'
    return result


def measured_input(raw, evidence):
    """Apply explicit shared evidence; never use another backend's numeric result."""
    raw = copy.deepcopy(raw)
    fmt = raw.setdefault('format', {})
    kind = fmt.get('format_name', '')
    if kind in ('mp3', 'aac', 'h264'):
        if evidence.get('status') == 'measured' and evidence.get('format') == kind:
            fmt['duration'] = evidence['duration_seconds']
        else:
            fmt['duration'] = None
        return raw, evidence
    return raw, {'status': 'unchanged', 'method': 'backend_container_duration'}


def mediainfo_raw(path, library, parse_speed="0.5"):
    # MediaInfo's wide-character C API uses stdcall on Windows.
    loader = C.WinDLL if os.name == 'nt' else C.CDLL
    lib = loader(library or ctypes.util.find_library('mediainfo') or
                 ('MediaInfo.dll' if os.name == 'nt' else 'libmediainfo.so.0'))
    signatures = {
        'New': (C.c_void_p, []), 'Delete': (None, [C.c_void_p]),
        'Open': (C.c_size_t, [C.c_void_p, C.c_wchar_p]),
        'Option': (C.c_wchar_p, [C.c_void_p, C.c_wchar_p, C.c_wchar_p]),
        'Inform': (C.c_wchar_p, [C.c_void_p, C.c_size_t]),
        'Get': (C.c_wchar_p, [C.c_void_p, C.c_int, C.c_size_t, C.c_wchar_p, C.c_int, C.c_int]),
    }
    for name, (result, args) in signatures.items():
        fn = getattr(lib, 'MediaInfo_' + name)
        fn.restype, fn.argtypes = result, args
    handle = lib.MediaInfo_New()
    if not handle:
        raise RuntimeError('MediaInfo_New failed')
    try:
        lib.MediaInfo_Option(handle, 'Output', 'JSON')
        lib.MediaInfo_Option(handle, 'ParseSpeed', parse_speed)
        # Preserve actual newlines. Default MediaInfo replaces them with ' / '.
        lib.MediaInfo_Option(handle, 'CarriageReturnReplace', '')
        if not lib.MediaInfo_Open(handle, str(path)):
            raise ValueError('MediaInfo could not recognize/open input')
        raw = json.loads(lib.MediaInfo_Inform(handle, 0))
        # Inform(JSON) base64-encodes control-containing values and can expose
        # internal escape placeholders. Read textual tags directly from the API.
        kinds = {'General': 0, 'Video': 1, 'Audio': 2}
        counts = {}
        for track in (raw.get('media') or {}).get('track', []):
            kind = track.get('@type')
            index = counts.get(kind, 0)
            counts[kind] = index + 1
            if kind in kinds:
                track['_native_tags'] = {key: lib.MediaInfo_Get(handle, kinds[kind], index, key, 1, 0) or ''
                    for key in ('Title', 'Track', 'Performer', 'Artist', 'Album',
                                'Director', 'OriginalSourceForm/Name', 'ARTIST', 'ALBUM', 'TITLE')}
        return raw
    finally:
        lib.MediaInfo_Delete(handle)


def mi_codec(track, warnings, container=""):
    fmt = track.get('Format', '')
    mapping = {'AVC': 'h264', 'HEVC': 'hevc', 'AV1': 'av1', 'VP8': 'vp8', 'VP9': 'vp9',
               'MPEG-4 Visual': 'mpeg4', 'AAC': 'aac', 'FLAC': 'flac', 'Opus': 'opus',
               'Vorbis': 'vorbis', 'Speex': 'speex', 'AC-3': 'ac3', 'E-AC-3': 'eac3',
               'DTS': 'dts', 'Theora': 'theora', 'FFV1': 'ffv1'}
    if fmt in mapping:
        return mapping[fmt]
    if fmt == 'MPEG Audio':
        profile = track.get('Format_Profile', '')
        for layer, codec in [('Layer 3', 'mp3'), ('Layer 2', 'mp2'), ('Layer 1', 'mp1')]:
            if layer in profile:
                return codec
    if fmt == 'MPEG Video':
        version = track.get('Format_Version', '')
        if '1' in version: return 'mpeg1video'
        if '2' in version: return 'mpeg2video'
    if fmt == 'PCM':
        bits = track.get('BitDepth', '')
        settings = track.get('Format_Settings', '')
        endian = track.get('Format_Settings_Endianness', '')
        sign = track.get('Format_Settings_Sign', '')
        if not sign and container == 'AIFF':
            sign = 'Signed'
        if not endian:
            endian = 'Little' if 'Little' in settings else 'Big' if 'Big' in settings else ''
        if bits == '8' and sign == 'Unsigned': return 'pcm_u8'
        if bits and endian and sign in ('Signed', 'Unsigned', 'Float'):
            return 'pcm_' + {'Signed': 's', 'Unsigned': 'u', 'Float': 'f'}[sign] + bits + ('le' if endian == 'Little' else 'be')
    warnings.append('Unmapped codec: ' + json.dumps(track, ensure_ascii=True))
    # Do not silently invent an FFmpeg name or report success as parity.
    return 'UNMAPPED:' + fmt


def mi_projection(raw):
    warnings = []
    tracks = (raw.get('media') or {}).get('track', [])
    general = next((t for t in tracks if t.get('@type') == 'General'), {})
    def tags(t):
        extra = {k.lower(): v for k, v in t.get('extra', {}).items()}
        if '_native_tags' in t:
            t = t['_native_tags']
            extra = {k.lower(): v for k, v in t.items() if k.isupper()}
        wave = general.get('Format') == 'Wave'
        return {'artist': t.get('Performer') or t.get('Artist') or extra.get('artist') or
                          (t.get('Director') if wave else '') or '',
                'album': t.get('Album') or extra.get('album') or
                         ((t.get('OriginalSourceForm_Name') or t.get('OriginalSourceForm/Name')) if wave else '') or '',
                'title': t.get('Title') or t.get('Track') or extra.get('title') or ''}
    streams = []
    for t in tracks:
        if t.get('@type') in ('Audio', 'Video'):
            streams.append({'codec_type': t['@type'].lower(), 'codec_name': mi_codec(t, warnings, general.get('Format', '')),
                            'tags': tags(t)})
    return {'format': {'format_name': {'MPEG Audio': 'mp3', 'ADTS': 'aac', 'AVC': 'h264'}.get(general.get('Format'), general.get('Format', '')),
                       'duration': general.get('Duration'),
                       'bit_rate': general.get('OverallBitRate'), 'tags': tags(general)},
            'streams': streams}, warnings


def run(command, timeout):
    started = time.monotonic()
    try:
        proc = subprocess.run(command, capture_output=True, timeout=timeout)
        data = {'elapsed_ms': round((time.monotonic() - started) * 1000, 2),
                'exit_code': proc.returncode, 'stderr': proc.stderr.decode('utf-8', 'replace')}
        if proc.returncode:
            return dict(data, error='process_failed')
        data['raw'] = json.loads(proc.stdout)
        return data
    except subprocess.TimeoutExpired:
        return {'error': 'timeout'}
    except (OSError, ValueError) as exc:
        return {'error': str(exc)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('files', nargs='*', type=Path)
    parser.add_argument('--libav-probe', default=str(Path(__file__).parent / 'build' / ('libav-probe.exe' if os.name == 'nt' else 'libav-probe')))
    parser.add_argument('--ffprobe', default='ffprobe')
    parser.add_argument('--mediainfo-library', default='')
    parser.add_argument('--policy', choices=('legacy', 'file-average', 'measured'), default='legacy',
                        help='measured adds bounded shared elementary-stream timing to file-average')
    parser.add_argument('--mi-parse-speed', choices=('0.5', '1'), default='0.5')
    parser.add_argument('--timeout', type=float, default=30)
    parser.add_argument('--report', type=Path, default=Path('media-probe-report.json'))
    parser.add_argument('--duration-worker', type=Path, help=argparse.SUPPRESS)
    parser.add_argument('--duration-max-bytes', type=int, default=512 * 1024 * 1024)
    parser.add_argument('--duration-max-frames', type=int, default=2_000_000)
    parser.add_argument('--mi-worker', type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.timeout <= 0 or args.duration_max_bytes <= 0 or args.duration_max_frames <= 0:
        parser.error('timeout and scan limits must be positive')
    if args.duration_worker:
        print(json.dumps(scan(args.duration_worker, args.duration_max_bytes, args.duration_max_frames, args.timeout)))
        return 0
    if args.mi_worker:
        print(json.dumps(mediainfo_raw(args.mi_worker, args.mediainfo_library, args.mi_parse_speed), ensure_ascii=True))
        return 0
    if not args.files:
        parser.error('supply media files or a directory')
    files = []
    for path in args.files:
        files.extend(sorted(p for p in path.rglob('*') if p.is_file()) if path.is_dir() else [path])
    if not files:
        parser.error('no input files found')
    report = {'schema_version': 3, 'policy': args.policy, 'mi_parse_speed': args.mi_parse_speed, 'files': []}
    for name, command in [('ffprobe', [args.ffprobe, '-version']), ('libav', [args.libav_probe, '--version'])]:
        try:
            p = subprocess.run(command, capture_output=True, timeout=args.timeout)
            report[name + '_version'] = p.stdout.decode('utf-8', 'replace').splitlines()[0]
        except (OSError, subprocess.TimeoutExpired, IndexError):
            report[name + '_version'] = 'unavailable'
    failures = 0
    for file in files:
        file = file.resolve()
        duration_scan = None
        if args.policy == 'measured':
            duration_scan = run([sys.executable, str(Path(__file__).resolve()),
                '--duration-worker', str(file), '--timeout', str(args.timeout),
                '--duration-max-bytes', str(args.duration_max_bytes),
                '--duration-max-frames', str(args.duration_max_frames)], args.timeout)
        backends = {
            'ffprobe': run([args.ffprobe, '-v', 'error', '-show_entries', ENTRIES, '-of', 'json', str(file)], args.timeout),
            'libav': run([args.libav_probe, str(file)], args.timeout),
            'mediainfo': run([sys.executable, str(Path(__file__).resolve()), '--mi-worker', str(file),
                              '--mediainfo-library', args.mediainfo_library, '--mi-parse-speed', args.mi_parse_speed], args.timeout),
        }
        for name, data in backends.items():
            if 'raw' in data:
                raw = data['raw']
                if name == 'mediainfo':
                    raw, data['warnings'] = mi_projection(raw)
                    data['projected'] = raw
                data['legacy_result'] = normalize(raw)
                size = file.stat().st_size if file.is_file() else None
                data['backend_result'] = normalize(raw, size, 'file-average' if args.policy == 'measured' else args.policy)
                if args.policy == 'measured':
                    evidence = duration_scan.get('raw', {'status': 'unavailable', 'reason': duration_scan.get('error')})
                    raw, data['duration_evidence'] = measured_input(raw, evidence)
                    if data['duration_evidence']['status'] in ('unsupported', 'unavailable'):
                        data['normalization_warning'] = data['duration_evidence'].get('reason')
                data['result'] = normalize(raw, size, 'file-average' if args.policy == 'measured' else args.policy)
        baseline = backends['ffprobe'].get('result')
        differences = {}
        for name in ('libav', 'mediainfo'):
            result = backends[name].get('result')
            if baseline is None or result is None:
                differences[name] = {'error': 'comparison unavailable; inspect backend errors'}
            else:
                diff = {k: {'ffprobe': baseline[k], name: result[k]} for k in (*FIELDS, 'outcome') if baseline[k] != result[k]}
                if backends[name].get('normalization_warning') or backends['ffprobe'].get('normalization_warning'):
                    diff['normalization_warning'] = backends[name].get('normalization_warning') or backends['ffprobe'].get('normalization_warning')
                if backends[name].get('warnings'):
                    diff['mapping_warnings'] = backends[name]['warnings']
                if diff: differences[name] = diff
        failures += bool(differences)
        backend_differences = {}
        base = backends['ffprobe'].get('backend_result')
        for name in ('libav', 'mediainfo'):
            other = backends[name].get('backend_result')
            backend_differences[name] = ({k: {'ffprobe': base[k], name: other[k]}
                for k in (*FIELDS, 'outcome') if base[k] != other[k]} if base and other else {'error': 'unavailable'})
        report['files'].append({'path': str(file), 'backends': backends, 'duration_scan': duration_scan,
                               'backend_differences': backend_differences, 'differences': differences})
        print(('DIFF ' if differences else 'PASS ') + str(file))
        for name, diff in differences.items():
            print('  ' + name + ': ' + ', '.join(diff))
    report['summary'] = {
        'files': len(files), 'passed': len(files) - failures, 'different_or_failed': failures,
        'backend_only_passed': sum(not any(f['backend_differences'].values()) for f in report['files']),
        'shared_duration_measured': sum((f['duration_scan'] or {}).get('raw', {}).get('status') == 'measured' for f in report['files']),
        'duration_unknown_by_policy': sum(any(b.get('duration_evidence', {}).get('status') == 'unknown'
                                             for b in f['backends'].values()) for f in report['files']),
    }
    args.report.write_text(json.dumps(report, ensure_ascii=True, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report['summary']) + '; report: ' + str(args.report))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
