#!/usr/bin/env python3
"""FX parameter tables: DrD85/nano-cortex-controller main/fx_models.c (MIT) -> components/nano_protocol/nano_fx_params.c.

Run from the project folder with a checkout of github.com/DrD85/nano-cortex-controller:
    python3 tools/gen_fx_params.py ../nano-cortex-controller/main/fx_models.c components/nano_protocol/nano_fx_params.c
"""
import re, sys, pathlib

src = pathlib.Path(sys.argv[1]).read_text()
out = pathlib.Path(sys.argv[2])

params = {}  # name -> list of entry strings
for m in re.finditer(r'static const nano_param_t (\w+)\[\] = \{\n(.*?)\n\};', src, re.S):
    rows = []
    for line in m.group(2).splitlines():
        line = line.strip().rstrip(',')
        assert line.startswith('{') and line.endswith('}'), line
        rows.append(line.replace('NANO_PARAM_RANGE', 'NANO_FX_PARAM_RANGE').replace('NANO_PARAM_ENUM', 'NANO_FX_PARAM_ENUM'))
    params[m.group(1)] = rows
orders = {m.group(1): m.group(2) for m in re.finditer(r'static const uint8_t (\w+)\[\] = \{ ([^}]*) \};', src)}
models = re.findall(r'\{ (\d+), "([^"]+)", NANO_ICON_\w+, 0x[0-9A-F]+, (-?\d+), (\d+), (\w+), (\w+) \}', src)
slots = re.findall(r'static const uint32_t NANO_SLOT(\d)_MODELS\[\] = \{ ([^}]*) \};', src)
assert len(models) == 53 and len(slots) == 5, (len(models), len(slots))

o = []
o.append('/* Generated from DrD85/nano-cortex-controller main/fx_models.c (MIT, Dominik Schmidt), which exports the Nano Cortex')
o.append(' * Editor\'s model data; regenerate rather than edit. See nano_fx_params.h. */')
o.append('#include "nano_fx_params.h"')
o.append('')
o.append('#include <stddef.h>')
o.append('')
used = set()
for type_, name, mix, count, p, order in models:
    if p not in used:
        used.add(p)
        o.append(f'static const nano_fx_param_t {p}[] = {{')
        for r in params[p]:
            o.append(f'    {r},')
        o.append('};')
    if order != 'NULL':
        o.append(f'static const uint8_t {order}[] = {{ {orders[order]} }};')
o.append('')
o.append('static const nano_fx_def_t DEFS[] = {')
for type_, name, mix, count, p, order in models:
    assert int(count) == len(params[p]), name
    o.append(f'    {{ {type_}, {mix}, {count}, {p}, {order} }}, /* {name} */')
o.append('};')
o.append('')
for n, lst in slots:
    o.append(f'static const uint16_t SLOT{n}[] = {{ {lst} }};')
o.append('static const uint16_t *const SLOTS[NANO_FX_SLOT_COUNT] = { SLOT0, SLOT1, SLOT2, SLOT3, SLOT4 };')
o.append('static const uint8_t SLOT_COUNTS[NANO_FX_SLOT_COUNT] = { ' + ', '.join(str(len(l.split(','))) for _, l in slots) + ' };')
o.append('''
const nano_fx_def_t *nano_fx_def(uint32_t type)
{
    for (size_t i = 0; type && i < sizeof(DEFS) / sizeof(DEFS[0]); i++) {
        if (DEFS[i].type == type) return &DEFS[i];
    }
    return NULL;
}

const uint16_t *nano_fx_slot_models(int slot, int *count)
{
    if (slot < 0 || slot >= NANO_FX_SLOT_COUNT) {
        *count = 0;
        return NULL;
    }
    *count = SLOT_COUNTS[slot];
    return SLOTS[slot];
}''')
out.write_text('\n'.join(o) + '\n')
print('models', len(models), 'param tables', len(used))
