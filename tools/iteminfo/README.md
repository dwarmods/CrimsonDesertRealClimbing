# ItemInfo reader/writer (Crimson Desert 1.0.0.3663 and 1.0.0.3732)

`iteminfo_codec.py` is a standalone codec for `iteminfo` rows and the `.staticinfoheader` /
`.staticinfobody` container (u16 row count, then u32 key + u32 body offset per row). A row
decodes to a plain tree and encodes back from that tree, so a byte-identical round trip shows
that every byte belongs to a modelled field.

- `grammar_3663.json` is the 3527/3663 row grammar exported from CDMW 0.12.0-alpha.5 by
  `gen_grammar.py` (MIT, see `THIRD_PARTY_CDMW_LICENSE.txt`). It holds field descriptions only.
- `grammar_3732()` applies the two 1.0.0.3732 (game 2.05.01) changes measured on the shipped
  table (see `docs/research/iteminfo_3732.md`).
- `port_rows_3732.py <tables> <out>` ports climbmod's three rows from a 0.3.0 (3663) ItemInfo to
  3732. It takes unchanged fields from the 3732 donor rows, sets the fields in
  `climbmod_items.json`, removes stat 1000037, and appends the rows to the game's own table.
  Inputs and outputs are local files and are never committed.

Tests (`tests/`) use synthetic rows generated from the grammar only.
