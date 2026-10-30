#!/usr/bin/env python3
"""Copy original, hash-verified acceptance inputs; derive codec test files."""
import argparse, hashlib, json, pathlib, shutil, struct, subprocess, zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('source', type=pathlib.Path, help='original fixture directory or bundled samples')
parser.add_argument('--output', required=True, type=pathlib.Path, help='new output directory; originals are never overwritten')
args = parser.parse_args()
source = args.source.resolve()
samples = args.output.resolve()
if samples.exists() or source == samples or source in samples.parents:
    parser.error('--output must be a new directory outside the input directory')
samples.mkdir(parents=True)
sha = lambda b: hashlib.sha256(b).hexdigest()
sources = json.loads((source / 'source-inputs.json').read_text())
rasters = json.loads((source / 'raster-manifest.json').read_text())
manifest = {'version': 1, 'description': 'Original 6 grid + 6 image acceptance fixtures plus browser codec variants.', 'samples': []}

def copy_verified(path, expected):
    data = (source / path).read_bytes()
    assert sha(data) == expected, path
    (samples / path).write_bytes(data)
    return data

for name, item in sources['sources'].items():
    namepath = pathlib.Path(item['path']).name
    data = copy_verified(namepath, item['sha256'])
    size = int(data.splitlines()[0])
    raw = bytes(int(v) for row in data.splitlines()[1:] for v in row.decode())
    assert len(raw) == size * size
    rawpath = name + '.raw'
    (samples / rawpath).write_bytes(raw)
    manifest['samples'].append({'id': name, 'label': name + '（行列）', 'kind': 'grid', 'format': 'grid', 'path': namepath, 'sha256': sha(data), 'acceptance': True, 'rawPath': rawpath, 'rawSha256': sha(raw), 'width': size, 'height': size, 'stride': size, 'channels': 1, 'expectedSymbols': [item['expectedSymbol']]})

for item in rasters['rasters']:
    path = pathlib.Path(item['path']).name
    data = copy_verified(path, item['sha256'])
    # These pinned fixtures have exactly four whitespace-delimited PNM fields.
    fields = data.split(b'\n', 3)
    assert fields[0] in (b'P5', b'P6') and fields[2] == b'255'
    w, h = map(int, fields[1].split())
    raw = fields[3]
    assert len(raw) == w * h * item['channels'] and sha(raw) == item['rasterSha256']
    name = path.rsplit('.', 1)[0] + ('-rgb' if item['channels'] == 3 else '-gray')
    rawpath = name + '.raw'
    (samples / rawpath).write_bytes(raw)
    manifest['samples'].append({'id': name, 'label': name + '（画像）', 'kind': 'image', 'format': 'pnm', 'path': path, 'sha256': sha(data), 'acceptance': True, 'rawPath': rawpath, 'rawSha256': sha(raw), 'width': w, 'height': h, 'stride': w * item['channels'], 'channels': item['channels'], 'expectedSymbols': [sources['sources'][p['source']]['expectedSymbol'] for p in item['placements']]})

def png(path, w, h, channels, raw):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    scan = b''.join(b'\0' + raw[y*w*channels:(y+1)*w*channels] for y in range(h))
    pngdata = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, {1:0, 3:2, 4:6}[channels], 0, 0, 0)) + chunk(b'IDAT', zlib.compress(scan)) + chunk(b'IEND', b'')
    (samples / path).write_bytes(pngdata)
    return sha(pngdata)

base = next(x for x in manifest['samples'] if x['id'] == 'single-binary-rgb')
raw = (samples / base['rawPath']).read_bytes()
pngsha = png('single-binary.png', base['width'], base['height'], 3, raw)
manifest['samples'].append({'id':'codec-png', 'label':'PNG の画像入力', 'kind':'image', 'format':'png', 'path':'single-binary.png', 'sha256':pngsha, 'codecTest':True})
subprocess.run(['/usr/bin/sips', '-s', 'format', 'jpeg', '-s', 'formatOptions', '85', str(samples/'single-binary.png'), '--out', str(samples/'single-binary.jpg')], check=True, capture_output=True)
manifest['samples'].append({'id':'codec-jpeg', 'label':'JPEG の画像入力', 'kind':'image', 'format':'jpeg', 'path':'single-binary.jpg', 'sha256':sha((samples/'single-binary.jpg').read_bytes()), 'codecTest':True})
# Make light pixels transparent while retaining a vivid hidden RGB color. The
# documented white composite should produce the identical black/white raster.
rgba = b''.join(bytes((255, 0, 190, 0)) if raw[i] == 255 else bytes((0, 0, 0, 255)) for i in range(0,len(raw),3))
transsha = png('single-binary-transparent.png', base['width'], base['height'], 4, rgba)
manifest['samples'].append({'id':'codec-transparent-png', 'label':'透明 PNG（白で合成）', 'kind':'image', 'format':'png', 'path':'single-binary-transparent.png', 'sha256':transsha, 'codecTest':True})
# Handoff service identifiers are not public provenance. Preserve all fixture
# bytes, content hashes, encoder identities and expectations when importing.
def public_provenance(value):
    if isinstance(value, dict):
        return {k: public_provenance(v) for k, v in value.items() if k != 'library_file_id'}
    if isinstance(value, list):
        return [public_provenance(v) for v in value]
    return value

source_data = (json.dumps(public_provenance(sources), ensure_ascii=False, indent=2)+'\n').encode()
(samples/'source-inputs.json').write_bytes(source_data)
rasters['sourceInputsSha256'] = sha(source_data)
(samples/'raster-manifest.json').write_text(json.dumps(rasters, ensure_ascii=False, indent=2)+'\n')
for name in ['qrcode-8.2-LICENSE.txt', 'segno-1.6.6-LICENSE.txt']:
    shutil.copyfile(source/name, samples/name)
(samples/'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n')
print(json.dumps({'fixtureCount':12, 'gridCount':6, 'imageCount':6, 'expectedSymbols':sum(len(x.get('expectedSymbols',[])) for x in manifest['samples']), 'codecSamples':3, 'manifest':str(samples/'manifest.json')}))
