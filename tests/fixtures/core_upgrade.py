#!/usr/bin/env python3
"""Prepare simulated newer core metadata while preserving all library bytes."""
import argparse
import hashlib
import json
from pathlib import Path
import re

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--prefix', type=Path, required=True)
parser.add_argument('--version', required=True)
parser.add_argument('--floor', required=True)
args = parser.parse_args()
manifest_path = args.prefix / 'share/cpkt/packages/core.json'
manifest = json.loads(manifest_path.read_text())
previous = manifest['release_version']
config = (args.prefix / 'lib/cmake/CpktCore/CpktCoreConfig.cmake').read_text()
previous_floor = re.search(r'CpktCore_COMPATIBLE_SINCE "([0-9.]+)"', config).group(1)
for entry in manifest['files']:
    path = args.prefix / entry['path']
    if entry['type'] != 'file':
        continue
    name = entry['path']
    if name.endswith(('CpktCoreConfigVersion.cmake', 'CpktCmockaConfigVersion.cmake')):
        content = path.read_text().replace('"' + previous + '"', '"' + args.version + '"')
        content = content.replace('PACKAGE_FIND_VERSION VERSION_LESS "' + previous_floor + '"',
                                  'PACKAGE_FIND_VERSION VERSION_LESS "' + args.floor + '"')
    elif name.endswith('CpktCoreConfig.cmake'):
        content = path.read_text().replace('CpktCore_COMPATIBLE_SINCE "' + previous_floor + '"',
                                          'CpktCore_COMPATIBLE_SINCE "' + args.floor + '"')
    elif name.startswith('lib/pkgconfig/') and name.endswith('.pc'):
        content = path.read_text().replace('cpkt-core = ' + previous, 'cpkt-core = ' + args.version)
        if name == 'lib/pkgconfig/cpkt-core.pc':
            content = content.replace('Version: ' + previous, 'Version: ' + args.version)
    else:
        continue
    path.write_text(content)
    entry['sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
manifest['release_version'] = args.version
del manifest['package_id']


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True,
                      separators=(',', ':'), allow_nan=False).encode('utf-8')


manifest['package_id'] = hashlib.sha256(canonical(manifest)).hexdigest()
manifest_path.write_bytes(canonical(manifest))
