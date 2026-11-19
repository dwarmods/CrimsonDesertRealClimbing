# ItemInfo format change in exe 1.0.0.3732 (game 2.05.01, Nov 19 2026)

Method: decode every 3663 row with the CDMW 0.12.0-alpha.5 grammar (7071/7071 round trip),
diff each 3732 row against the same key's 3663 row, then confirm the new grammar by
(a) a byte-identical round trip of every 3732 row and (b) a field-by-field comparison of the
decoded 3732 tree against the 3663 tree.

## What changed

The container is unchanged: header u16 count + (u32 key, u32 offset) per row, and rows are
contiguous in the body.

Row layout, two insertions only:

1. **ItemInfo**: one new `u8` right after `_discardable` (before `_useAveragePrice`).
   It is 1 on 133 base rows and 0 on everything else (0 on all DLC rows). The rows with 1 are
   gimmick, puzzle and attachable parts (category 3001 and similar). The real member name is
   unknown, so the codec calls it `_unk3732_afterDiscardable`.
2. **PageData** (`_fixedPageDataList` / `_dynamicPageDataList` elements): one new `u8` after
   `_rightPageRelatedKnowledgeInfo`, 0 on every shipped page (`_unk3732_pageTail`). The
   positions after `_rightPageTexturePath` and after `_leftPageRelatedKnowledgeInfo` also
   round-trip. Only the end position keeps every knowledge-info value equal to 3663, so that
   is the one used.

No field was removed, resized or moved. Earlier diff hits inside InspectData and InspectAction
came from runs of zero bytes and disappear under the PageData change.

Row length deltas: +1 on 6054 rows, +1 + pages on rows with page data, and about +115 on
804 rows whose content changed in the patch (page data added, inspect target page updated).

## Results

| Table | Rows | Round trip | Semantically equal to 3663 |
|---|---|---|---|
| iteminfo (base) 3732 | 7068 | 7068/7068 byte-identical | 6264 (the other 804 have real content changes) |
| iteminfo.dlc_01 3732 | 1555 | 1555/1555 byte-identical | n/a |
| iteminfo 3663 (old grammar) | 7071 | 7071/7071 | n/a |

The 3663 grammar rejects all 7068 3732 rows, and the 3732 grammar rejects 3663 rows, so the
two layouts cannot be mixed up.

## Tool status on 2.05.01

- CDMW 0.12.0-alpha.5 fails every 3732 row (the 1-byte shift breaks `_itemName` and `_defaultSubItem`).
- DMM 5.0.2 ("made for 2.05.00") logs `the game's iteminfo does not read (offset 0xc1)` and skips
  every ItemInfo edit (whole-table copies and format-3 intents alike), because it parses the
  game's table first. The other tables keep their layout and apply normally.
