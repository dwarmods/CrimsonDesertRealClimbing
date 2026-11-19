"""Offline tests for the ItemInfo codec. Synthetic rows only: no game data is used or stored."""
import pathlib, sys, unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import iteminfo_codec as ic  # noqa: E402


def synth(g, op, salt=[0]):
    """Build a deterministic tree for any grammar op (one element per list, every optional present)."""
    salt[0] += 1
    c = op[0]
    if c == 'raw':
        n = op[1]
        if n == 0:
            return None
        return bytes((salt[0] + i) & 0xFF for i in range(n)) if n > 8 else (salt[0] * 2654435761) % (1 << (8 * n))
    if c == 'text':
        return f't{salt[0]}'
    if c == 'ref':
        return synth(g, g['defs'][op[1]])
    if c == 'seq':
        out = []
        for child in op[1:]:
            if child[0] == 'field':
                out.append([child[2], synth(g, child[3])])
            else:
                out.append([None, synth(g, child)])
        return out
    if c == 'field':
        return [[op[2], synth(g, op[3])]]
    if c == 'array':
        return [synth(g, op[2])]
    if c == 'repeat':
        return [synth(g, op[2]) for _ in range(op[1])]
    if c == 'optional':
        return synth(g, op[1])
    if c == 'variant':
        tag = sorted(op[2], key=int)[0]
        return {'tag': int(tag), 'value': synth(g, op[2][tag])}
    if c == 'tagged':
        tag = sorted(op[3], key=int)[0]
        return {'tag': int(tag), 'head': synth(g, op[2]), 'value': synth(g, op[3][tag])}
    raise ValueError(c)


def walk_pages(tree, fn):
    for page_list in ic.find_all(tree, '_fixedPageDataList') + ic.find_all(tree, '_dynamicPageDataList'):
        fn(page_list)


class Codec(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.g63 = ic.load_grammar()
        cls.g32 = ic.grammar_3732()

    def test_grammar_is_schema_only(self):
        text = (pathlib.Path(ic.__file__).parent / 'grammar_3663.json').read_text()
        self.assertLess(len(text), 64 * 1024)
        self.assertIn('MIT', text)

    def test_synthetic_roundtrip_both_generations(self):
        for g in (self.g63, self.g32):
            tree = synth(g, g['root'])
            raw = ic.encode_row(tree, g)
            back, ew = ic.decode_row(raw, g)
            self.assertEqual(back, tree)
            self.assertEqual(ic.encode_row(back, g, ew), raw)

    def test_3732_layout_delta(self):
        t32 = synth(self.g32, self.g32['root'])
        raw32 = ic.encode_row(t32, self.g32)
        # Same row without the 3732 fields must be a valid 3663 row exactly 1 + pages bytes shorter.
        t63 = ic.deepcopy(t32)
        t63 = [p for p in t63 if p[0] != '_unk3732_afterDiscardable']
        def strip_tail(node):
            if isinstance(node, list):
                node[:] = [x for x in node if not (isinstance(x, list) and len(x) == 2 and x[0] == '_unk3732_pageTail')]
                for x in node:
                    strip_tail(x)
            elif isinstance(node, dict):
                for v in node.values():
                    strip_tail(v)
        pages = len(ic.find_all(t32, '_unk3732_pageTail'))
        strip_tail(t63)
        raw63 = ic.encode_row(t63, self.g63)
        self.assertEqual(len(raw32) - len(raw63), 1 + pages)
        self.assertGreaterEqual(pages, 1)
        names = [n for n, _ in t32]
        self.assertEqual(names[names.index('_discardable') + 1], '_unk3732_afterDiscardable')

    def test_generations_do_not_cross_parse(self):
        raw32 = ic.encode_row(synth(self.g32, self.g32['root']), self.g32)
        raw63 = ic.encode_row(synth(self.g63, self.g63['root']), self.g63)
        for raw, g in ((raw32, self.g63), (raw63, self.g32)):
            try:
                ic.decode_row(raw, g)
                parsed = True
            except ic.RowError:
                parsed = False
            self.assertFalse(parsed)

    def test_truncated_and_trailing_rows_refused(self):
        raw = ic.encode_row(synth(self.g32, self.g32['root']), self.g32)
        with self.assertRaises(ic.RowError):
            ic.decode_row(raw[:-1], self.g32)
        with self.assertRaises(ic.RowError):
            ic.decode_row(raw + b'\0', self.g32)

    def test_table_container_roundtrip(self):
        rows = {7: b'abc', 3: b'', 9: b'xyz12'}
        body, header = ic.write_table([7, 3, 9], rows)
        keys, back = ic.read_table(body, header)
        self.assertEqual(keys, [7, 3, 9])
        self.assertEqual(back, rows)
        self.assertEqual(ic.write_table(keys, back), (body, header))

    def test_tree_helpers(self):
        rec = [['_a', 1], [None, [['_b', 2]]]]
        self.assertEqual(ic.get(rec, '_a'), 1)
        ic.put(rec, '_a', 5)
        self.assertEqual(ic.find_all(rec, '_b'), [2])
        self.assertEqual(rec[0][1], 5)


if __name__ == '__main__':
    unittest.main()
