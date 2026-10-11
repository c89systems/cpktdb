#!/usr/bin/env python3
"""Generate compile probes from tracked database API data; CMake owns compilation."""
import argparse
import json
from pathlib import Path
import re
from generated_output_paths import write_generated_text

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--contracts', type=Path, required=True)
parser.add_argument('--target', required=True)
parser.add_argument('--provider', required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
facade = json.loads((args.contracts / 'db_facade_api.json').read_text())[args.target][args.provider]
native = json.loads((args.contracts / 'db_native_api.json').read_text())[args.target][args.provider]
constants = json.loads((args.contracts / 'db_constant_bindings.json').read_text())[args.provider]
facade_lines = ['#include <cpkt/' + args.provider + '.h>', 'void cpkt_db_contract_probe(void) {']
for name in sorted(facade['functions']):
    facade_lines.append('  (void)&' + name + ';')
for record, fields in sorted(facade['records'].items()):
    for field in fields:
        facade_lines.append('  (void)sizeof(((' + record.split(':', 1)[1] + ' *)0)->' + field + ');')
for name in sorted(facade['typedefs']):
    facade_lines.append('  (void)sizeof(' + name + ' *);')
facade_lines.append('}')
write_generated_text(args.output, '\n'.join(facade_lines) + '\n')
lines = ['#include <cpkt/' + args.provider + '.h>']
headers = ['libpq-fe.h', 'libpq-events.h', 'postgres_ext.h', 'libpq/libpq-fs.h'] if args.provider == 'postgres' else ['sqlite3.h', 'sqlite3session.h', 'sqlite3rtree.h', 'fts5.h', 'sqlite3ext.h']
lines += ['#include <' + name + '>' for name in headers]
lines.append('#include <limits.h>')
for index, (name, decision) in enumerate(sorted(constants.items())):
    mapped = decision.get('facade', '')
    if not re.fullmatch(r'[A-Za-z_]\w*', mapped):
        continue
    if native['macros'][name].startswith('"'):
        lines.append('const char *cpkt_db_string_%d = %s;' % (index, mapped))
    else:
        lines.append('typedef char cpkt_db_constant_%d[((%s) == (%s)) ? 1 : -1];' % (index, name, mapped))
lines.append('void cpkt_db_contract_probe(void) {')
for name in sorted(facade['functions']):
    lines.append('  (void)&' + name + ';')
for record, fields in sorted(facade['records'].items()):
    for field in fields:
        lines.append('  (void)sizeof(((' + record.split(':', 1)[1] + ' *)0)->' + field + ');')
for name in sorted(facade['typedefs']):
    lines.append('  (void)sizeof(' + name + ' *);')
lines.append('}')
write_generated_text(args.output.with_name(args.output.stem + '-native.c'), '\n'.join(lines) + '\n')
