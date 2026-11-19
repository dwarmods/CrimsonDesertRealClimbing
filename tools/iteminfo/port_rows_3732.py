"""Port the three climbmod ItemInfo rows (gear pack 0.3.0, 3663 format) to the 3732 format.

Local-only inputs: the 0.3.0 pack's ItemInfo (3663) and the game's 3732 ItemInfo.
Output: a 3732 ItemInfo table = vanilla 3732 rows (byte-identical) + our 3 rows.
"""
import json, sys, pathlib, hashlib
sys.path.insert(0, str(pathlib.Path(__file__).parent))
from iteminfo_codec import *

T = pathlib.Path(sys.argv[1])        # tables dir with g3663pack/ and g3732/
OUT = pathlib.Path(sys.argv[2])
OUR = {1990000: 210071, 1990001: 210071, 1990002: 131027}   # key -> donor template
DROP_STAT = 1000037                                       # global stamina stat, replaced by the plugin
g63, g32 = load_grammar(), grammar_3732()

def tab(d):
    return read_table((T / d / 'iteminfo.staticinfobody').read_bytes(), (T / d / 'iteminfo.staticinfoheader').read_bytes())

def strip(node):
    if isinstance(node, list):
        return [strip(i) for i in node if not (isinstance(i, list) and len(i) == 2 and isinstance(i[0], str) and i[0].startswith('_unk3732'))]
    if isinstance(node, dict):
        return {k: strip(v) for k, v in node.items()}
    return node

def drop_stat(enchant, key):
    removed = 0
    for stat_list in find_all(enchant, '_statList_DataDefinedStatic'):
        entries = stat_list[0][1]
        keep = [e for e in entries if e[0] != [None, [[None, key]]]]
        removed += len(entries) - len(keep)
        stat_list[0][1] = keep
    return removed

k63, r63 = tab('g3663pack'); k32, r32 = tab('g3732')
report = {'rows': {}}
new_rows = {}
for key, donor in OUR.items():
    ours, _ = decode_row(r63[key], g63)
    d63, _ = decode_row(r63[donor], g63)
    d32, ew = decode_row(r32[donor], g32)
    if strip(d32) != d63:
        raise SystemExit(f'donor {donor} changed between 3663 and 3732; review before porting')
    changed = [n for (n, v), (_, dv) in zip(ours, d63) if v != dv]
    row = deepcopy(d32)
    for n in changed:
        put(row, n, deepcopy(get(ours, n)))
    removed = drop_stat(get(row, '_enchantDataList'), DROP_STAT)
    raw = encode_row(row, g32, ew)
    back, _ = decode_row(raw, g32)
    assert back == row and encode_row(back, g32, ew) == raw
    # Everything except the dropped stat must equal the 0.3.0 row's meaning.
    ref = deepcopy(ours); drop_stat(get(ref, '_enchantDataList'), DROP_STAT)
    assert strip(back) == ref, key
    new_rows[key] = raw
    levels = get(row, '_enchantDataList')[0][1]
    report['rows'][key] = dict(string_key=get(row, '_stringKey'), donor=donor, fields_from_030=changed,
                               stat_1000037_entries_removed=removed, enchant_levels=len(levels), bytes=len(raw))

keys = list(k32) + list(OUR)
rows = dict(r32); rows.update(new_rows)
body, header = write_table(keys, rows)
k2, r2 = read_table(body, header)
assert k2 == keys and all(r2[k] == r32[k] for k in k32)
for k in keys:
    t, ew = decode_row(r2[k], g32); assert encode_row(t, g32, ew) == r2[k]
OUT.mkdir(parents=True, exist_ok=True)
(OUT / 'iteminfo.staticinfobody').write_bytes(body); (OUT / 'iteminfo.staticinfoheader').write_bytes(header)
report.update(vanilla_rows=len(k32), total_rows=len(keys), body_sha256=hashlib.sha256(body).hexdigest(),
              header_sha256=hashlib.sha256(header).hexdigest(), body_bytes=len(body), header_bytes=len(header))
(OUT / 'build_rows_3732.json').write_text(json.dumps(report, indent=1))
print(json.dumps(report, indent=1))
