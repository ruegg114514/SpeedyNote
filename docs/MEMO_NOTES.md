# Memos (随记)

Free-form notes kept beside the page: a screenshot region or a lasso-cut piece
of handwriting is stored as an image in the notebook and shown both as a
thumbnail gallery in the right sidebar and as a floating card over the page.

This is deliberately separate from the markdown notes (which hang off a
`LinkObject` slot and exist to annotate the PDF text): a memo belongs to the
notebook as a whole, has no anchor on the page, and can be dragged anywhere.

## Storage

```
<notebook>.snb/
├── document.json            # "memos": [ {id, image, title, created, kind,
│                            #              source_page?, float_*?}, ... ]
└── assets/memos/<uuid>.png  # one PNG per memo
```

* `memos` is written only when the notebook has at least one memo, so a
  notebook that never used the feature keeps its previous `document.json`
  shape.
* `image` is relative to `assets/` (`memos/<uuid>.png`), matching how
  `ImageObject` stores its images.
* `float_x/y/w/h` + `float_rotation` are the last card geometry **in viewport
  logical pixels**, and are only written once the card has actually been moved
  or resized. Reopening clamps the card into the viewport.
* A memo created before the document has ever been saved keeps its PNG in
  memory (`MemoEntry::pendingImage`) until `Document::saveBundle()` writes it.
* `.snbx` export copies the whole bundle, so memos travel with the notebook.
  PDF export does not include floating cards (they are screen-space overlays).

`Document` owns the list (`memos()`, `memoById()`, `memoImage()`, `addMemo()`,
`updateMemoFloat()`, `renameMemo()`, `removeMemo()`); nothing about the pages
changes when a memo is created.

## Creating a memo

* **Screenshot tool** – the region overlay now offers, next to Cancel/Confirm:
  * **Memo (keep)** – insert the capture on the page as before *and* keep a copy
    as a memo.
  * **Memo (cut)** – keep the capture as a memo only; nothing is inserted on
    the page. The page content itself is untouched (the shot is a copy of the
    rendered page).
  Cancel / Confirm / Enter / double-click keep their old meanings; the memo
  buttons are dimmed until a region has been dragged.
* **Lasso tool** – the lasso action bar gains two buttons:
  * keep: the strokes stay on the page, a copy is stored as a memo;
  * cut: the strokes are removed from the page (one undoable action) and stored
    as a memo. The selection is rendered with any live rotate/scale/move
    applied, into a transparent PNG at 2 px per document unit (capped at
    3072 px per side).
* **Import** – the `+` button in the gallery header imports image files
  (`MemoEntry::Kind::Imported`).

New memos are **not** opened automatically (a card popping up mid-stroke is
worse than a quiet confirmation); a transient chip in the bottom-centre of the
canvas says "Added to Memos" and the gallery shows the new thumbnail.

## Gallery (right sidebar, "Memos" tab)

* The right sidebar's right-side button now opens a two-tab panel:
  **Memos** (default) and **Notes** (the pre-existing markdown notes tree and
  search).
* The gallery lists memos newest-first as a thumbnail grid whose column count
  follows the sidebar width (1–4 columns), with title + timestamp.
* Hover reveals a `×` (the window asks for confirmation); the context menu has
  Open / Rename… / Delete… and, when the memo records a source page, "Go to
  source page".
* Clicking a thumbnail closes the sidebar (so the card lands on the full-width
  page) and opens the floating card.

## Floating card

* Drag the body to move, drag any of the 8 handles to resize, drag the circular
  handle above the card to rotate (15° steps, same snap as rotated objects in
  the canvas), press the always-visible `×` to put it away. The memo itself is
  not deleted by closing its card.
* The card is a viewport-space overlay: it does **not** scroll with the page.
  Wheel events over a card are forwarded to the viewport, so scrolling the page
  under a card still works. A press on the page deactivates the card (hides its
  handles).
* Geometry and rotation are persisted per memo; cards of the other tabs are
  remembered for the session and restored when switching back. Reopening a
  notebook starts with no cards open.

## Known limits

* Touch panning started on a card drags the card rather than panning the page
  (mouse/trackpad wheel is forwarded; multi-finger gestures are not).
* A card is drawn over the page, so ink under it cannot be written until it is
  moved.
* Memo images are decoded synchronously when the gallery rebuilds; a notebook
  with hundreds of large memos would want a thumbnail cache.
* Moving a card marks the notebook modified (the new position has to be saved).
