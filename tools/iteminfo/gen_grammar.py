"""Export the ItemInfo row grammar reachable from CDMW's compact static schema to JSON.

CDMW (Crimson Desert Mod Workbench) is MIT licensed, Copyright (c) 2026 Ratrider.
The exported file only describes field names, widths and container shapes. It holds
no game data. Usage: python gen_grammar.py <path-to-CDMW-source> > grammar_3663.json
"""
import json, sys
sys.path.insert(0, sys.argv[1])
from cdmw.core._compact_static_schema import READERS  # noqa: E402
from cdmw.core.compact_static import _ITEMINFO_3527  # noqa: E402

used = {}


def conv(op):
    c = op[0]
    if c == 'call':
        if op[1] not in used:
            used[op[1]] = None
            used[op[1]] = conv(READERS[op[1]])
        return ['ref', str(op[1])]
    if c == 'raw':
        return ['raw', op[1]]
    if c == 'text':
        return ['text']
    if c == 'seq':
        return ['seq'] + [conv(x) for x in op[1:]]
    if c == 'field':
        return ['field', op[1], op[2], conv(op[3])]
    if c == 'array':
        return ['array', op[1], conv(op[2])]
    if c == 'repeat':
        return ['repeat', op[1], conv(op[2])]
    if c == 'optional':
        return ['optional', conv(('call', op[1]))]
    if c == 'presence':
        return ['optional', conv(op[1])]
    if c == 'variant':
        return ['variant', op[1], {str(k): conv(v) for k, v in op[-1].items()}]
    if c == 'tagged':
        return ['tagged', op[1], conv(op[2]), {str(k): conv(v) for k, v in op[-1].items()}]
    raise ValueError(op)


root = conv(_ITEMINFO_3527)
json.dump({'source': 'CDMW 0.12.0-alpha.5 compact schema (MIT, (c) 2026 Ratrider), ItemInfo 1.0.0.3527/3663',
           'root': root, 'defs': used}, sys.stdout, indent=None, separators=(',', ':'))
