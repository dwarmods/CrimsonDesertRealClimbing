"""Standalone ItemInfo row reader/writer for Crimson Desert static tables.

A row is decoded into a plain tree and encoded back from that tree, so a round trip
that reproduces the input bytes shows that every byte belongs to a modelled field.

Tree shapes:
  record   -> list of [name, value] pairs (name None for unnamed members)
  raw n    -> int (n <= 8) or bytes (n > 8); n == 0 -> None
  text     -> str (u32 length + UTF-8)
  array    -> list (u32/u16/u8 count prefix as in the grammar)
  repeat   -> list (fixed count)
  optional -> None or value (u8 presence flag)
  variant  -> {"tag": int, "value": v}
  tagged   -> {"tag": int, "head": v, "value": v}
"""
import copy
import json
import pathlib
import struct

HERE = pathlib.Path(__file__).resolve().parent


class RowError(ValueError):
    pass


def load_grammar(name='grammar_3663.json'):
    return json.loads((HERE / name).read_text())


# ---------------------------------------------------------------- grammar edits
def _find_field(op, owner, name, path=()):
    """Yield (container, index) for every field op with this owner/name."""
    if isinstance(op, list) and op:
        if op[0] == 'field' and op[1] == owner and op[2] == name:
            yield path
        for i, child in enumerate(op):
            if isinstance(child, list):
                yield from _find_field(child, owner, name, path + (i,))
            elif isinstance(child, dict):
                for k, v in child.items():
                    yield from _find_field(v, owner, name, path + (i, k))


def _get(op, path):
    for p in path:
        op = op[p]
    return op


def insert_field_after(g, owner, after, new_name, shape):
    """Insert ['field', owner, new_name, shape] right after field `after` of `owner`."""
    hits = 0
    for where in [g['root']] + list(g['defs'].values()):
        for path in list(_find_field(where, owner, after)):
            parent = _get(where, path[:-1])
            idx = path[-1]
            parent.insert(idx + 1, ['field', owner, new_name, shape])
            hits += 1
    if hits != 1:
        raise RowError(f'grammar edit {owner}.{after}: {hits} matches')
    return g


def grammar_3732():
    """ItemInfo grammar for exe 1.0.0.3732, derived from 3663 plus the measured changes.

    See docs/research/iteminfo_3732.md for how each change was established.
    """
    g = load_grammar()
    insert_field_after(g, 'ItemInfo', '_discardable', '_unk3732_afterDiscardable', ['raw', 1])
    for owner, after, name in EDITS_3732_SUB:
        insert_field_after(g, owner, after, name, ['raw', 1])
    return g


# Filled in from the corpus measurement (owner, insert-after field, new name).
EDITS_3732_SUB = [('PageData', '_rightPageRelatedKnowledgeInfo', '_unk3732_pageTail')]


# ---------------------------------------------------------------- codec
class _Reader:
    def __init__(self, g, raw, enchant_width):
        self.g, self.raw, self.pos, self.ew = g, raw, 0, enchant_width

    def take(self, n):
        if n < 0 or self.pos + n > len(self.raw):
            raise RowError(f'truncated at 0x{self.pos:X} (+{n})')
        b = self.raw[self.pos:self.pos + n]
        self.pos += n
        return b

    def count(self, w):
        v = int.from_bytes(self.take(w), 'little')
        if v > 1 << 20:
            raise RowError(f'list count {v} too large at 0x{self.pos - w:X}')
        return v

    def read(self, op, owner=None, name=None):
        c = op[0]
        if c == 'raw':
            n = op[1]
            if owner == 'DropInfoData' and name == '_enchantLevel':
                n = self.ew
            b = self.take(n)
            if n == 0:
                return None
            return int.from_bytes(b, 'little') if n <= 8 else b
        if c == 'text':
            n = self.count(4)
            return self.take(n).decode('utf-8')
        if c == 'ref':
            return self.read(self.g['defs'][op[1]])
        if c == 'seq':
            out = []
            for child in op[1:]:
                if child[0] == 'field':
                    out.append([child[2], self.read(child[3], child[1], child[2])])
                else:
                    out.append([None, self.read(child)])
            return out
        if c == 'field':
            return [[op[2], self.read(op[3], op[1], op[2])]]
        if c == 'array':
            return [self.read(op[2]) for _ in range(self.count(op[1]))]
        if c == 'repeat':
            return [self.read(op[2]) for _ in range(op[1])]
        if c == 'optional':
            flag = self.take(1)[0]
            if flag not in (0, 1):
                raise RowError(f'presence flag {flag} at 0x{self.pos - 1:X}')
            return self.read(op[1]) if flag else None
        if c == 'variant':
            tag = int.from_bytes(self.take(op[1]), 'little')
            if str(tag) not in op[2]:
                raise RowError(f'unknown variant tag {tag} at 0x{self.pos - op[1]:X}')
            return {'tag': tag, 'value': self.read(op[2][str(tag)])}
        if c == 'tagged':
            tag = int.from_bytes(self.take(op[1]), 'little')
            if str(tag) not in op[3]:
                raise RowError(f'unknown tagged tag {tag} at 0x{self.pos - op[1]:X}')
            head = self.read(op[2])
            return {'tag': tag, 'head': head, 'value': self.read(op[3][str(tag)])}
        raise RowError(f'unknown op {c}')


class _Writer:
    def __init__(self, g, enchant_width):
        self.g, self.out, self.ew = g, bytearray(), enchant_width

    def write(self, op, v, owner=None, name=None):
        c = op[0]
        if c == 'raw':
            n = op[1]
            if owner == 'DropInfoData' and name == '_enchantLevel':
                n = self.ew
            if n == 0:
                return
            if isinstance(v, (bytes, bytearray)):
                if len(v) != n:
                    raise RowError(f'{name}: expected {n} bytes')
                self.out += v
            else:
                self.out += int(v).to_bytes(n, 'little')
            return
        if c == 'text':
            b = v.encode('utf-8')
            self.out += struct.pack('<I', len(b)) + b
            return
        if c == 'ref':
            return self.write(self.g['defs'][op[1]], v)
        if c == 'seq':
            if len(v) != len(op) - 1:
                raise RowError('record shape mismatch')
            for child, (nm, val) in zip(op[1:], v):
                if child[0] == 'field':
                    if nm != child[2]:
                        raise RowError(f'field order: got {nm}, grammar {child[2]}')
                    self.write(child[3], val, child[1], child[2])
                else:
                    self.write(child, val)
            return
        if c == 'field':
            return self.write(op[3], v[0][1], op[1], op[2])
        if c == 'array':
            self.out += len(v).to_bytes(op[1], 'little')
            for x in v:
                self.write(op[2], x)
            return
        if c == 'repeat':
            if len(v) != op[1]:
                raise RowError('repeat count mismatch')
            for x in v:
                self.write(op[2], x)
            return
        if c == 'optional':
            if v is None:
                self.out.append(0)
            else:
                self.out.append(1)
                self.write(op[1], v)
            return
        if c == 'variant':
            self.out += int(v['tag']).to_bytes(op[1], 'little')
            return self.write(op[2][str(v['tag'])], v['value'])
        if c == 'tagged':
            self.out += int(v['tag']).to_bytes(op[1], 'little')
            self.write(op[2], v['head'])
            return self.write(op[3][str(v['tag'])], v['value'])
        raise RowError(f'unknown op {c}')


def decode_row(raw, g, enchant_widths=(2, 3)):
    """Decode one row; returns (tree, enchant_width). Exactly one width must consume the row."""
    found, errors = [], []
    for ew in enchant_widths:
        r = _Reader(g, bytes(raw), ew)
        try:
            tree = r.read(g['root'])
            if r.pos != len(raw):
                raise RowError(f'{len(raw) - r.pos} trailing bytes at 0x{r.pos:X}')
            found.append((tree, ew))
        except (RowError, UnicodeDecodeError) as e:
            errors.append(f'w{ew}: {e}')
    if not found:
        raise RowError('; '.join(errors))
    if len(found) > 1 and found[0][0] != found[1][0]:
        # Both widths consumed the row with different trees: ambiguous.
        raise RowError('ambiguous DropInfoData width')
    return found[0]


def encode_row(tree, g, enchant_width=2):
    w = _Writer(g, enchant_width)
    w.write(g['root'], tree)
    return bytes(w.out)


# ---------------------------------------------------------------- tree helpers
def get(rec, name):
    for k, v in rec:
        if k == name:
            return v
    raise KeyError(name)


def put(rec, name, value):
    for pair in rec:
        if pair[0] == name:
            pair[1] = value
            return
    raise KeyError(name)


def find_all(node, name, out=None):
    """All values of fields called `name` anywhere below node."""
    out = [] if out is None else out
    if isinstance(node, list):
        for item in node:
            if isinstance(item, list) and len(item) == 2 and (item[0] is None or isinstance(item[0], str)):
                if item[0] == name:
                    out.append(item[1])
                find_all(item[1], name, out)
            else:
                find_all(item, name, out)
    elif isinstance(node, dict):
        for v in node.values():
            find_all(v, name, out)
    return out


# ---------------------------------------------------------------- table container
def read_table(body, header):
    """pabgh header: u16 count, then count x (u32 key, u32 body offset)."""
    n = struct.unpack_from('<H', header, 0)[0]
    if len(header) != 2 + 8 * n:
        raise RowError('unexpected header size')
    ents = [struct.unpack_from('<II', header, 2 + 8 * i) for i in range(n)]
    order = sorted(range(n), key=lambda i: ents[i][1])
    rows = {}
    for j, i in enumerate(order):
        k, off = ents[i]
        end = ents[order[j + 1]][1] if j + 1 < n else len(body)
        rows[k] = body[off:end]
    return [k for k, _ in ents], rows


def write_table(keys, rows):
    body, header = bytearray(), bytearray(struct.pack('<H', len(keys)))
    for k in keys:
        header += struct.pack('<II', k, len(body))
        body += rows[k]
    return bytes(body), bytes(header)


def deepcopy(tree):
    return copy.deepcopy(tree)
