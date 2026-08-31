# Pandoc Converter Gaps Implementation Plan (Phase 1 of 2)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `pandoc_ast_to_blocks()` and `duck_blocks_to_pandoc_ast()` lose no content — map the four missing pandoc constructors, add passthroughs so no future constructor can vanish silently, and emit an api-version pandoc 3.x will actually accept.

**Architecture:** All work is in the two Pandoc converters plus the ANSI renderer. The dispatch chains in `ProcessPandocBlockVal` (blocks) and `FlattenPandocInlinesVal` (inlines) currently end in bare fallbacks that discard input; they become explicit passthrough emitters. `Figure` gets structural conversion using the same `level`-based nesting `Div` already uses, with a new general-purpose `caption` container block.

**Tech Stack:** C++17, DuckDB extension API, yyjson (vendored), sqllogictest (`.test` files), Python 3 for the pandoc alignment harness.

**Spec:** `docs/superpowers/specs/2026-08-31-pandoc-gaps-and-reader-dispatch-design.md`

## Global Constraints

- **No new third-party dependencies.** `duck_block_utils` stays dependency-free; yyjson is already vendored. The alignment harness is standalone Python 3 with no packages, and skips cleanly when `pandoc` is absent.
- **Every recursive converter path must call `CheckPandocDepth(depth)`** before recursing — see `src/include/pandoc_convert_util.hpp:21-26`. This caps runaway nesting.
- **Build:** `make` (release). **Test:** `make test`. **Format:** `make format-fix`, verified by `make format-check` — CI enforces clang-format, so run it before every commit.
- **Pandoc api-version floor is `[1,23]`.** Verified: pandoc 3.1.3 rejects `[1,20]` and `[1,22]` outright with "Incompatible API versions".
- **Element-type names are lowercase strings** defined as `static constexpr const char *` in `src/include/block_types.hpp`. Never inline a literal; always reference the constant.
- Conventional commit prefixes (`feat:`, `fix:`, `docs:`, `test:`) — match existing history.

---

### Task 1: `Underline` round-trips in both directions

The smallest complete vertical slice. `INLINE_UNDERLINE` already exists in the vocabulary (`block_types.hpp:75`), is already produced by `db_underline()`, and is already styled by the ANSI renderer — only the two Pandoc converters never match it. Doing this first proves the test loop before harder tasks.

**Files:**
- Modify: `src/pandoc_inline_convert.cpp` (import branch near `:95-101`; export group at `:343-345`)
- Test: `test/sql/pandoc_blocks_v2.test`

**Interfaces:**
- Consumes: `BlockTypes::INLINE_UNDERLINE` (already defined, value `"underline"`)
- Produces: nothing new for later tasks

- [ ] **Step 1: Write the failing tests**

Append to `test/sql/pandoc_blocks_v2.test`:

```
# ============================================================================
# Underline (import + export) — see specs/2026-08-31-pandoc-gaps
# ============================================================================

query I
SELECT pandoc_ast_to_blocks('[{"t":"Para","c":[{"t":"Underline","c":[{"t":"Str","c":"under"}]},{"t":"Str","c":"!"}]}]')[2].element_type;
----
underline

query I
SELECT pandoc_ast_to_blocks('[{"t":"Para","c":[{"t":"Underline","c":[{"t":"Str","c":"under"}]},{"t":"Str","c":"!"}]}]')[3].content;
----
under

query I
SELECT duck_blocks_to_pandoc_ast(db_paragraph([db_text('An '), db_underline('underlined'), db_text(' phrase.')])).blocks::VARCHAR LIKE '%"t":"Underline"%';
----
true

query I
SELECT duck_blocks_to_pandoc_ast(db_paragraph([db_text('An '), db_underline('underlined'), db_text(' phrase.')])).blocks::VARCHAR LIKE '%[underline]%';
----
false
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `make test 2>&1 | grep -A5 pandoc_blocks_v2`
Expected: FAIL. Import returns `text` not `underline`; export contains the literal `[underline]` placeholder.

- [ ] **Step 3: Add the import branch**

In `src/pandoc_inline_convert.cpp`, immediately after the `Emph` branch (currently around `:100-101`), add:

```cpp
		} else if (strcmp(pandoc_type, "Underline") == 0) {
			inline_type = BlockTypes::INLINE_UNDERLINE;
			yyjson_val *inlines = c_val;
			result.push_back(CreateDocInline(inline_type, "", level, attrs, order++));
			if (inlines) {
				FlattenPandocInlinesVal(inlines, level + 1, order, result, depth + 1);
			}
			return;
```

Follow the exact shape of the neighbouring `Strong`/`Emph` branches — they are container inlines that emit a marker then recurse, so `Underline` must do the same or its text becomes a sibling rather than a child.

- [ ] **Step 4: Add the export branch**

In the same file, the styled-inline group currently reads:

```cpp
		} else if (inline_type == BlockTypes::INLINE_BOLD || inline_type == BlockTypes::INLINE_ITALIC ||
		           inline_type == BlockTypes::INLINE_STRIKETHROUGH || inline_type == BlockTypes::INLINE_SUPERSCRIPT ||
		           inline_type == BlockTypes::INLINE_SUBSCRIPT || inline_type == BlockTypes::INLINE_SMALLCAPS) {
			const char *p_type = "Strong";
```

Add `INLINE_UNDERLINE` to that condition, and add its mapping to the `p_type` chain below it:

```cpp
		} else if (inline_type == BlockTypes::INLINE_BOLD || inline_type == BlockTypes::INLINE_ITALIC ||
		           inline_type == BlockTypes::INLINE_STRIKETHROUGH || inline_type == BlockTypes::INLINE_SUPERSCRIPT ||
		           inline_type == BlockTypes::INLINE_SUBSCRIPT || inline_type == BlockTypes::INLINE_SMALLCAPS ||
		           inline_type == BlockTypes::INLINE_UNDERLINE) {
			const char *p_type = "Strong";
			if (inline_type == BlockTypes::INLINE_ITALIC) {
				p_type = "Emph";
			} else if (inline_type == BlockTypes::INLINE_UNDERLINE) {
				p_type = "Underline";
			} else if (inline_type == BlockTypes::INLINE_STRIKETHROUGH) {
```

- [ ] **Step 5: Build, format, test**

```bash
make && make format-fix && make format-check && make test
```
Expected: all four new assertions PASS, no existing test regresses.

- [ ] **Step 6: Commit**

```bash
git add src/pandoc_inline_convert.cpp test/sql/pandoc_blocks_v2.test
git commit -m "fix(pandoc): map Underline in both converter directions"
```

---

### Task 2: Unknown block constructors pass through instead of vanishing

The highest-value change in this plan. `ProcessPandocBlockVal` ends in `} else { return; }`, so any unrecognised block is discarded with no trace. This is what hides `Figure`, `LineBlock` and `DefinitionList` today, and would hide any constructor a future pandoc adds.

**Files:**
- Modify: `src/include/block_types.hpp` (add `TYPE_GENERIC`)
- Modify: `src/pandoc_block_convert.cpp` (the `else` at `:306`)
- Test: `test/sql/pandoc_blocks_v2.test`

**Interfaces:**
- Produces: `BlockTypes::TYPE_GENERIC` = `"generic"`, used by Task 3's inline counterpart and referenced by Task 9's docs.

**Naming — decided during execution, superseding the spec's `pandoc:unknown`.** That name
bakes a format into the core vocabulary, which is the leakage this whole effort removes, and
it would be stranded here when the Pandoc converters move to panduck. `generic` is
format-neutral, so `html_to_duck_blocks`, sitting_duck and panduck's own readers can all
reach for the same escape hatch. Block and inline **share the string `"generic"`**,
disambiguated by `kind` — following the existing precedent of `TYPE_CODE`/`INLINE_CODE`,
`TYPE_IMAGE`/`INLINE_IMAGE` and `TYPE_RAW`/`INLINE_RAW`, which already do this. Distinct
from `raw`, which means literal content in a *named* format; `generic` means a structured
element we cannot name. The original constructor goes in `attributes['source_type']`, also
format-neutral where `pandoc_type` would not be.
- A `generic` block carries `encoding='json'`, `content` = the verbatim constructor JSON, and `attributes['source_type']` = the original `t` string.

- [ ] **Step 1: Write the failing tests**

Append to `test/sql/pandoc_blocks_v2.test`:

```
# ============================================================================
# Unknown block constructors pass through (no silent drops)
# ============================================================================

query I
SELECT len(pandoc_ast_to_blocks('[{"t":"NotARealConstructor","c":[1,2]}]'));
----
1

query I
SELECT pandoc_ast_to_blocks('[{"t":"NotARealConstructor","c":[1,2]}]')[1].element_type;
----
generic

query I
SELECT pandoc_ast_to_blocks('[{"t":"NotARealConstructor","c":[1,2]}]')[1].attributes['source_type'];
----
NotARealConstructor

query I
SELECT pandoc_ast_to_blocks('[{"t":"NotARealConstructor","c":[1,2]}]')[1].encoding;
----
json

# document length is preserved: 3 blocks in, 3 blocks out
query I
SELECT len(pandoc_ast_to_blocks('[{"t":"Para","c":[{"t":"Str","c":"a"}]},{"t":"Bogus"},{"t":"Para","c":[{"t":"Str","c":"b"}]}]'));
----
3
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `make test 2>&1 | grep -A5 pandoc_blocks_v2`
Expected: FAIL — first assertion returns `0`, last returns `2`.

- [ ] **Step 3: Add the constant**

In `src/include/block_types.hpp`, in the "Block type names" section after `TYPE_DIV`:

```cpp
	static constexpr const char *TYPE_DIV = "div";
	static constexpr const char *TYPE_GENERIC = "generic";
```

- [ ] **Step 4: Replace the dropping `else`**

In `src/pandoc_block_convert.cpp`, replace:

```cpp
	} else {
		return;
	}
```

with:

```cpp
	} else {
		// Never drop a constructor silently: preserve it verbatim so document
		// length is stable and the gap is visible instead of invisible.
		block_type = BlockTypes::TYPE_GENERIC;
		encoding = "json";
		attrs["source_type"] = string(pandoc_type);
		content = ValToJsonString(block_val);
	}
```

Note it serialises `block_val` (the whole constructor object), not `c_val`, so export can reconstitute the original including its `t`.

- [ ] **Step 5: Build, format, test**

```bash
make && make format-fix && make format-check && make test
```
Expected: all five new assertions PASS.

- [ ] **Step 6: Commit**

```bash
git add src/include/block_types.hpp src/pandoc_block_convert.cpp test/sql/pandoc_blocks_v2.test
git commit -m "fix(pandoc): pass unknown block constructors through as generic"
```

---

### Task 3: Unknown inlines keep their text

Two separate failures share this task. The structured path replaces an unknown inline with the literal placeholder `[Underline]`, destroying the words. The flattening path (`ExtractInlinesTextVal`) drops them with no marker at all — the more serious of the two, because nothing is left to notice.

**Files:**
- Modify: `src/include/block_types.hpp` (add `INLINE_GENERIC`)
- Modify: `src/pandoc_inline_convert.cpp` (the `else` at `:240-241`)
- Modify: `src/pandoc_block_convert.cpp` (`ExtractInlinesTextVal`, `:135`)
- Modify: `src/pandoc_inline_convert.cpp` (`RenderInlinesToTextVal`, `:577`)
- Test: `test/sql/pandoc_blocks_v2.test`

**Interfaces:**
- Consumes: nothing from Task 2 (independent)
- Produces: `BlockTypes::INLINE_GENERIC` = `"generic"`

- [ ] **Step 1: Write the failing tests**

Append to `test/sql/pandoc_blocks_v2.test`:

```
# ============================================================================
# Unknown inline constructors keep their text
# ============================================================================

# structured path: marker present AND words preserved as children
query I
SELECT list_contains(
  list_transform(pandoc_ast_to_blocks(
    '[{"t":"Para","c":[{"t":"Bogus","c":[{"t":"Str","c":"keepme"}]},{"t":"Code","c":[["",[],[]],"x"]}]}]'),
    b -> b.content),
  'keepme');
----
true

# no bracket placeholder is emitted
query I
SELECT pandoc_ast_to_blocks(
  '[{"t":"Para","c":[{"t":"Bogus","c":[{"t":"Str","c":"keepme"}]},{"t":"Code","c":[["",[],[]],"x"]}]}]')::VARCHAR LIKE '%[Bogus]%';
----
false

# flattening path: text-only run keeps the words in block content
query I
SELECT pandoc_ast_to_blocks('[{"t":"Para","c":[{"t":"Str","c":"An"},{"t":"Space"},{"t":"Bogus","c":[{"t":"Str","c":"kept"}]},{"t":"Space"},{"t":"Str","c":"here."}]}]')[1].content;
----
An kept here.
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `make test 2>&1 | grep -A5 pandoc_blocks_v2`
Expected: FAIL — placeholder check returns `true`, flattened content reads `An  here.` with the doubled space.

- [ ] **Step 3: Add the constant**

In `src/include/block_types.hpp`, after `INLINE_RAW`:

```cpp
	static constexpr const char *INLINE_RAW = "raw";
	static constexpr const char *INLINE_GENERIC = "generic";
```

- [ ] **Step 4: Replace the structured-path placeholder**

In `src/pandoc_inline_convert.cpp`, replace:

```cpp
		} else {
			inline_type = BlockTypes::INLINE_TEXT;
			content_str = "[" + string(pandoc_type) + "]";
		}
```

with a marker-plus-recursion form matching how `Span` is handled — emit the marker, then flatten children so the words survive as real inlines:

```cpp
		} else {
			// Preserve the words: emit a visible marker, then recurse into the
			// constructor's inline children rather than replacing them.
			attrs["source_type"] = string(pandoc_type);
			result.push_back(CreateDocInline(BlockTypes::INLINE_GENERIC, "", level, attrs, order++));
			if (c_val) {
				FlattenPandocInlinesVal(c_val, level + 1, order, result, depth + 1);
			}
			return;
		}
```

- [ ] **Step 5: Fix `RenderInlinesToTextVal` (NOT `ExtractInlinesTextVal`)**

**Corrected during execution.** This step originally called for fixing *both*
`ExtractInlinesTextVal` (`pandoc_block_convert.cpp:135`) and `RenderInlinesToTextVal`
(`pandoc_inline_convert.cpp`). Measuring showed only the second is a real bug.

`ExtractInlinesTextVal` needs no change. Its output is written to a block's `content`, but
`ProcessPandocBlockVal` then calls `InlinesAreTextOnly`, which returns false for *any*
container inline and clears `content`. So `content` survives only for runs that are purely
`Str`/`Space`/`SoftBreak`/`LineBreak` — runs with no container to recurse into. Making
Underline and unknown inlines into containers (Tasks 1 and 2) is what actually fixes the
report's "Case A", and it was verified: the run that produced `An  here.` now yields
`text:An / space / underline / text:underlined / space / text:here.`

`RenderInlinesToTextVal` **is** a live bug, independently of the block path. It backs the
public scalar `pandoc_inlines_to_text()`, whose dispatch chain ends at `Link` with no
fallback, so unknown constructors contribute nothing. Verified before the fix:

```
pandoc_inlines_to_text('[Str "An", Space, Underline[Str "underlined"], Space, Str "here."]')
  -> 'An  here.'        <- text destroyed, same shape as Case A
```

Add a terminal `else` that recurses into `c` when it is an array:

```cpp
		} else if (c_val && yyjson_is_arr(c_val)) {
			// Unrecognised constructor (Underline, or anything a future pandoc adds):
			// recurse into its children so the words survive instead of vanishing.
			// A `c` that is not an inline list simply yields nothing, so this degrades
			// gracefully rather than emitting structural noise.
			out << RenderInlinesToTextVal(c_val, mode, depth + 1);
		}
```

One `else` covers both `Underline` and any future constructor, so no explicit `Underline`
branch is needed here.

- [ ] **Step 6: Build, format, test**

```bash
make && make format-fix && make format-check && make test
```
Expected: all three new assertions PASS.

- [ ] **Step 7: Commit**

```bash
git add src/include/block_types.hpp src/pandoc_inline_convert.cpp src/pandoc_block_convert.cpp test/sql/pandoc_blocks_v2.test
git commit -m "fix(pandoc): preserve text of unknown inline constructors"
```

---

### Task 4: `LineBlock`

**Files:**
- Modify: `src/include/block_types.hpp` (add `TYPE_LINEBLOCK`)
- Modify: `src/pandoc_block_convert.cpp` (new dispatch branch before the `else`)
- Test: `test/sql/pandoc_blocks_v2.test`

**Interfaces:**
- Produces: `BlockTypes::TYPE_LINEBLOCK` = `"lineblock"`
- Pandoc shape: `LineBlock c = [[Inline]]` — an array of lines, each an array of inlines.

- [ ] **Step 1: Write the failing tests**

```
# ============================================================================
# LineBlock
# ============================================================================

query I
SELECT pandoc_ast_to_blocks('[{"t":"LineBlock","c":[[{"t":"Str","c":"one"}],[{"t":"Str","c":"two"}]]}]')[1].element_type;
----
lineblock

query I
SELECT pandoc_ast_to_blocks('[{"t":"LineBlock","c":[[{"t":"Str","c":"one"}],[{"t":"Str","c":"two"}]]}]')[1].content;
----
one
two
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `make test 2>&1 | grep -A5 pandoc_blocks_v2`
Expected: FAIL — with Task 2 landed these now return `generic`, not `0` rows.

- [ ] **Step 3: Add the constant**

```cpp
	static constexpr const char *TYPE_LINEBLOCK = "lineblock";
```

- [ ] **Step 4: Add the dispatch branch**

In `src/pandoc_block_convert.cpp`, before the final `else`:

```cpp
	} else if (strcmp(pandoc_type, "LineBlock") == 0) {
		block_type = BlockTypes::TYPE_LINEBLOCK;
		if (c_val && yyjson_is_arr(c_val)) {
			string joined;
			size_t idx, max;
			yyjson_val *line;
			yyjson_arr_foreach(c_val, idx, max, line) {
				if (idx > 0) {
					joined += "\n";
				}
				joined += ExtractInlinesTextVal(line);
			}
			content = joined;
		}
```

Note this deliberately does **not** set `inlines_val_p`: a LineBlock's `c` is an array *of arrays*, so handing it to the inline converter would misparse. Line structure lives in `content` as newline-separated text.

- [ ] **Step 5: Build, format, test**

```bash
make && make format-fix && make format-check && make test
```
Expected: both assertions PASS.

- [ ] **Step 6: Commit**

```bash
git add src/include/block_types.hpp src/pandoc_block_convert.cpp test/sql/pandoc_blocks_v2.test
git commit -m "feat(pandoc): map LineBlock to lineblock blocks"
```

---

### Task 5: `DefinitionList`

Follows the existing `BulletList`/`OrderedList` precedent — stored as JSON, because its `[([Inline],[[Block]])]` shape has no flat text rendering.

**Files:**
- Modify: `src/include/block_types.hpp` (add `TYPE_DEFLIST`)
- Modify: `src/pandoc_block_convert.cpp`
- Test: `test/sql/pandoc_blocks_v2.test`

**Interfaces:**
- Produces: `BlockTypes::TYPE_DEFLIST` = `"deflist"`, `encoding='json'`

- [ ] **Step 1: Write the failing tests**

```
# ============================================================================
# DefinitionList
# ============================================================================

query I
SELECT pandoc_ast_to_blocks('[{"t":"DefinitionList","c":[[[{"t":"Str","c":"term"}],[[{"t":"Plain","c":[{"t":"Str","c":"def"}]}]]]]}]')[1].element_type;
----
deflist

query I
SELECT pandoc_ast_to_blocks('[{"t":"DefinitionList","c":[[[{"t":"Str","c":"term"}],[[{"t":"Plain","c":[{"t":"Str","c":"def"}]}]]]]}]')[1].encoding;
----
json

query I
SELECT pandoc_ast_to_blocks('[{"t":"DefinitionList","c":[[[{"t":"Str","c":"term"}],[[{"t":"Plain","c":[{"t":"Str","c":"def"}]}]]]]}]')[1].content LIKE '%term%';
----
true
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `make test 2>&1 | grep -A5 pandoc_blocks_v2`
Expected: FAIL — returns `generic`.

- [ ] **Step 3: Add the constant**

```cpp
	static constexpr const char *TYPE_DEFLIST = "deflist";
```

- [ ] **Step 4: Add the dispatch branch**

Beside the existing list branch in `src/pandoc_block_convert.cpp`:

```cpp
	} else if (strcmp(pandoc_type, "DefinitionList") == 0) {
		block_type = BlockTypes::TYPE_DEFLIST;
		encoding = "json";
		content = ValToJsonString(c_val);
```

- [ ] **Step 5: Build, format, test**

```bash
make && make format-fix && make format-check && make test
```
Expected: all three assertions PASS.

- [ ] **Step 6: Commit**

```bash
git add src/include/block_types.hpp src/pandoc_block_convert.cpp test/sql/pandoc_blocks_v2.test
git commit -m "feat(pandoc): map DefinitionList to deflist blocks"
```

---

### Task 6: `Figure` with structured captions

The largest task, and the highest-impact one — pandoc 3.x wraps every standalone captioned image in a `Figure`, and duckeye routes thirteen formats through this converter.

Pandoc shape: `Figure c = [Attr, Caption, [Block]]` where `Caption = [ShortCaption?, [Block]]`. Verified against pandoc 3.1.3: `![A **bold** caption](img.png)` yields `c[1] = [null, [Plain [Str, Space, Strong[...]]]]`.

A figure holds *two* block lists, so the flat `duck_block` list needs them distinguishable. A `caption` container block nested by `level` does that, exactly as `Div` already nests its children.

**Files:**
- Modify: `src/include/block_types.hpp` (add `TYPE_FIGURE`, `TYPE_CAPTION`)
- Modify: `src/pandoc_block_convert.cpp` (dispatch branch + export path)
- Test: `test/sql/pandoc_blocks_v2.test`

**Interfaces:**
- Produces: `BlockTypes::TYPE_FIGURE` = `"figure"`, `BlockTypes::TYPE_CAPTION` = `"caption"`
- Emission order is **content blocks first, then the `caption` container**, both at `level+1`. Task 7 (renderer) depends on this order.
- `caption` carries `attributes['short_caption']` only when pandoc's ShortCaption is non-null.

- [ ] **Step 1: Write the failing tests**

```
# ============================================================================
# Figure with structured captions
# ============================================================================

query I
SELECT pandoc_ast_to_blocks('[{"t":"Figure","c":[["",[],[]],[null,[{"t":"Plain","c":[{"t":"Str","c":"cap"}]}]],[{"t":"Plain","c":[{"t":"Image","c":[["",[],[]],[{"t":"Str","c":"cap"}],["img.png",""]]}]}]]}]')[1].element_type;
----
figure

# the image survives as a real inline, not opaque JSON
query I
SELECT list_contains(
  list_transform(pandoc_ast_to_blocks('[{"t":"Figure","c":[["",[],[]],[null,[{"t":"Plain","c":[{"t":"Str","c":"cap"}]}]],[{"t":"Plain","c":[{"t":"Image","c":[["",[],[]],[{"t":"Str","c":"cap"}],["img.png",""]]}]}]]}]'),
    b -> b.element_type),
  'image');
----
true

# a caption container is emitted
query I
SELECT list_contains(
  list_transform(pandoc_ast_to_blocks('[{"t":"Figure","c":[["",[],[]],[null,[{"t":"Plain","c":[{"t":"Str","c":"cap"}]}]],[{"t":"Plain","c":[{"t":"Image","c":[["",[],[]],[{"t":"Str","c":"cap"}],["img.png",""]]}]}]]}]'),
    b -> b.element_type),
  'caption');
----
true

# caption FORMATTING survives — the regression test for structured captions
query I
SELECT list_contains(
  list_transform(pandoc_ast_to_blocks('[{"t":"Figure","c":[["",[],[]],[null,[{"t":"Plain","c":[{"t":"Strong","c":[{"t":"Str","c":"bold"}]}]}]],[{"t":"Plain","c":[{"t":"Str","c":"body"}]}]]}]'),
    b -> b.element_type),
  'bold');
----
true

# a figure with an empty caption emits no caption container
query I
SELECT list_contains(
  list_transform(pandoc_ast_to_blocks('[{"t":"Figure","c":[["",[],[]],[null,[]],[{"t":"Plain","c":[{"t":"Str","c":"body"}]}]]}]'),
    b -> b.element_type),
  'caption');
----
false

# nested inside a Div, the figure is no longer dropped
query I
SELECT list_contains(
  list_transform(pandoc_ast_to_blocks('[{"t":"Div","c":[["",["note"],[]],[{"t":"Figure","c":[["",[],[]],[null,[]],[{"t":"Plain","c":[{"t":"Str","c":"inner"}]}]]}]]}]'),
    b -> b.element_type),
  'figure');
----
true
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `make test 2>&1 | grep -A5 pandoc_blocks_v2`
Expected: FAIL — returns `generic` and neither `image` nor `caption` appears.

- [ ] **Step 3: Add the constants**

```cpp
	static constexpr const char *TYPE_FIGURE = "figure";
	static constexpr const char *TYPE_CAPTION = "caption";
```

- [ ] **Step 4: Add the dispatch branch**

In `src/pandoc_block_convert.cpp`, modelled on the `Div` branch (which is the only other container that recurses) and placed immediately before it:

```cpp
	} else if (strcmp(pandoc_type, "Figure") == 0) {
		block_type = BlockTypes::TYPE_FIGURE;
		if (c_val && yyjson_is_arr(c_val) && yyjson_arr_size(c_val) >= 3) {
			yyjson_val *attr_val = yyjson_arr_get(c_val, 0);
			yyjson_val *caption_val = yyjson_arr_get(c_val, 1);
			yyjson_val *blocks_arr = yyjson_arr_get(c_val, 2);

			PandocAttr pattr;
			ParsePandocAttrVal(attr_val, pattr);
			StorePandocAttr(pattr, attrs);

			result.push_back(CreateDocBlock(block_type, "", attrs, order++, encoding, block_level));

			// Content blocks first, so a renderer walking the flat list emits the
			// image before its caption -- the correct visual order.
			if (blocks_arr && yyjson_is_arr(blocks_arr)) {
				size_t idx, max;
				yyjson_val *child_block;
				yyjson_arr_foreach(blocks_arr, idx, max, child_block) {
					ProcessPandocBlockVal(child_block, order, result, depth + 1, effective_level);
				}
			}

			// Caption = [ShortCaption?, [Block]]. Emit a container only when the
			// caption actually has blocks, then recurse so formatting survives.
			if (caption_val && yyjson_is_arr(caption_val) && yyjson_arr_size(caption_val) >= 2) {
				yyjson_val *short_val = yyjson_arr_get(caption_val, 0);
				yyjson_val *cap_blocks = yyjson_arr_get(caption_val, 1);
				if (cap_blocks && yyjson_is_arr(cap_blocks) && yyjson_arr_size(cap_blocks) > 0) {
					map<string, string> cap_attrs;
					if (short_val && yyjson_is_arr(short_val)) {
						string short_text = ExtractInlinesTextVal(short_val);
						if (!short_text.empty()) {
							cap_attrs["short_caption"] = short_text;
						}
					}
					result.push_back(CreateDocBlock(BlockTypes::TYPE_CAPTION, "", cap_attrs, order++, "text",
					                                Value(effective_level)));
					size_t idx, max;
					yyjson_val *cap_block;
					yyjson_arr_foreach(cap_blocks, idx, max, cap_block) {
						ProcessPandocBlockVal(cap_block, order, result, depth + 1, effective_level);
					}
				}
			}
			return;
		}
```

- [ ] **Step 5: Add the export branch**

Find the block-export function that maps `element_type` back to pandoc constructors in `src/pandoc_block_convert.cpp` (the reverse of the above, near where `TYPE_DIV` is exported). Add a `figure` case that consumes its `level+1` children, splitting them on the `caption` child:

```cpp
	} else if (element_type == BlockTypes::TYPE_FIGURE) {
		// Children at level+1 are content blocks, optionally followed by a
		// `caption` container whose own children are the caption blocks.
		yyjson_mut_val *obj = yyjson_mut_obj(doc);
		yyjson_mut_obj_add_str(doc, obj, "t", "Figure");
		yyjson_mut_val *c_arr = yyjson_mut_arr(doc);
		yyjson_mut_arr_add_val(c_arr, BuildPandocAttr(doc, block));

		yyjson_mut_val *content_arr = yyjson_mut_arr(doc);
		yyjson_mut_val *caption_arr = yyjson_mut_arr(doc);
		CollectFigureChildren(doc, blocks, i, level, content_arr, caption_arr, end_idx, depth + 1);

		yyjson_mut_val *cap = yyjson_mut_arr(doc);
		yyjson_mut_arr_add_val(cap, yyjson_mut_null(doc));
		yyjson_mut_arr_add_val(cap, caption_arr);
		yyjson_mut_arr_add_val(c_arr, cap);
		yyjson_mut_arr_add_val(c_arr, content_arr);
		yyjson_mut_obj_add_val(doc, obj, "c", c_arr);
		yyjson_mut_arr_add_val(arr, obj);
```

**Mirror `ConvertDivToPandocVal`** — it is the existing container-export function and the exact pattern to follow. Read it in full first (declared near the top of the export section, alongside `ConvertListToPandocVal`). It takes `start_idx` **by reference**, walks forward while child `level` exceeds the container's, and breaks when `child_level <= div_level`. Getting that bookkeeping wrong silently truncates documents.

Add a sibling helper with the same signature:

```cpp
static yyjson_mut_val *ConvertFigureToPandocVal(yyjson_mut_doc *doc, const vector<Value> &blocks_list,
                                               idx_t &start_idx, int32_t fig_level, idx_t depth) {
	CheckPandocDepth(depth);
	auto &fig_block = blocks_list[start_idx];

	yyjson_mut_val *content_arr = yyjson_mut_arr(doc);
	yyjson_mut_val *caption_arr = yyjson_mut_arr(doc);
	yyjson_mut_val *short_val = yyjson_mut_null(doc);

	idx_t j = start_idx + 1;
	bool in_caption = false;
	while (j < blocks_list.size()) {
		auto &child = blocks_list[j];
		if (child.IsNull()) {
			j++;
			continue;
		}
		auto child_kind = GetElementStringField(child, BlockTypes::KIND_IDX);
		auto child_type = GetElementStringField(child, BlockTypes::ELEMENT_TYPE_IDX);
		int32_t child_level = GetElementLevel(child);

		// Leaving the figure entirely.
		if (child_kind == BlockTypes::KIND_BLOCK && child_level <= fig_level) {
			break;
		}
		// The caption container itself: switch target, do not emit it.
		if (child_kind == BlockTypes::KIND_BLOCK && child_type == BlockTypes::TYPE_CAPTION &&
		    child_level == fig_level + 1) {
			auto short_text = GetElementAttribute(child, "short_caption");
			if (!short_text.empty()) {
				short_val = yyjson_mut_strncpy(doc, short_text.data(), short_text.size());
			}
			in_caption = true;
			j++;
			continue;
		}
		// Everything else routes to the active target.
		ConvertBlockAtIndexToPandoc(doc, blocks_list, j, in_caption ? caption_arr : content_arr, depth + 1);
	}
	start_idx = j;

	yyjson_mut_val *obj = yyjson_mut_obj(doc);
	yyjson_mut_obj_add_str(doc, obj, "t", "Figure");
	yyjson_mut_val *c_arr = yyjson_mut_arr(doc);
	yyjson_mut_arr_add_val(c_arr, BuildPandocAttrVal(doc, fig_block));
	yyjson_mut_val *cap = yyjson_mut_arr(doc);
	yyjson_mut_arr_add_val(cap, short_val);
	yyjson_mut_arr_add_val(cap, caption_arr);
	yyjson_mut_arr_add_val(c_arr, cap);
	yyjson_mut_arr_add_val(c_arr, content_arr);
	yyjson_mut_obj_add_val(doc, obj, "c", c_arr);
	return obj;
}
```

Two names above must be reconciled with what the file actually has, since the export dispatch at `:936` currently inlines its per-block conversion rather than exposing a helper:

- `ConvertBlockAtIndexToPandoc(doc, blocks_list, j, target_arr, depth)` — must advance `j`. If no such helper exists, extract one from the body of the main export loop at `:936` first, as a separate refactor commit with no behavior change, then use it here. Do not duplicate the dispatch chain.
- `BuildPandocAttrVal(doc, block)` — use whatever the `Div` export already calls to rebuild an `Attr` triple; reuse it rather than writing a second one.

Then wire it into the export dispatch beside `TYPE_DIV` at `:936`:

```cpp
		} else if (element_type == BlockTypes::TYPE_FIGURE) {
			int32_t fig_level = GetElementLevel(block);
			yyjson_mut_val *fig_obj = ConvertFigureToPandocVal(doc, blocks_list, block_idx, fig_level, 1);
			yyjson_mut_arr_add_val(blocks_arr, fig_obj);
```

- [ ] **Step 6: Build, format, test**

```bash
make && make format-fix && make format-check && make test
```
Expected: all six assertions PASS.

- [ ] **Step 7: Verify against real pandoc**

```bash
printf '![A **bold** caption](img.png)\n' > /tmp/fig.md
pandoc -f markdown -t json /tmp/fig.md > /tmp/fig.json
./build/release/duckdb -c "SELECT b.kind, b.element_type, b.content FROM (SELECT unnest(pandoc_ast_to_blocks((SELECT content FROM read_text('/tmp/fig.json')))) AS b) t;"
```
Expected: a `figure` block, a `paragraph` with an `image` inline, a `caption` block, and a `bold` inline under it. Four pandoc blocks in the earlier repro must now yield four duck_blocks, not one.

- [ ] **Step 8: Commit**

```bash
git add src/include/block_types.hpp src/pandoc_block_convert.cpp test/sql/pandoc_blocks_v2.test
git commit -m "feat(pandoc): map Figure structurally with a caption container block"
```

---

### Task 7: Render `figure` and `caption` in the ANSI renderer

Without this, figures stop being *dropped* but stay *invisible*: `render_ansi.cpp:1125` does `if (lines.empty()) continue;`, so the empty-content `figure` and `caption` blocks emit nothing, and the caption's child paragraph renders as undistinguished body text. A reader cannot tell a caption from surrounding prose.

**Files:**
- Modify: `src/render_ansi.cpp` (block dispatch chain, `:1094-1120`)
- Test: `test/sql/render_ansi.test`

**Interfaces:**
- Consumes: `BlockTypes::TYPE_FIGURE`, `BlockTypes::TYPE_CAPTION` from Task 6; relies on Task 6's content-then-caption emission order.

- [ ] **Step 1: Write the failing test**

Append to `test/sql/render_ansi.test`:

```
# ============================================================================
# figure / caption rendering
# ============================================================================

# a caption is visually distinguished (dim SGR 2), not plain body text
query I
SELECT db_blocks_render_ansi(pandoc_ast_to_blocks(
  '[{"t":"Figure","c":[["",[],[]],[null,[{"t":"Plain","c":[{"t":"Str","c":"capword"}]}]],[{"t":"Plain","c":[{"t":"Str","c":"bodyword"}]}]]}]')
) LIKE '%capword%';
----
true

query I
SELECT db_blocks_render_ansi(pandoc_ast_to_blocks(
  '[{"t":"Figure","c":[["",[],[]],[null,[{"t":"Plain","c":[{"t":"Str","c":"capword"}]}]],[{"t":"Plain","c":[{"t":"Str","c":"bodyword"}]}]]}]')
) LIKE '%bodyword%';
----
true
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make test 2>&1 | grep -A5 render_ansi`
Expected: both may already pass for the *text* (children render as paragraphs) — confirm by eye that the caption is styled identically to the body, which is the defect. If both pass, the failing assertion to add is the styling one in Step 3.

- [ ] **Step 3: Add the render cases**

In `src/render_ansi.cpp`, in the `element_type` chain before the final `else if (!text.empty())`:

```cpp
		} else if (element_type == BlockTypes::TYPE_FIGURE) {
			// Transparent container: children render themselves.
		} else if (element_type == BlockTypes::TYPE_CAPTION) {
			// Transparent container too, but mark the run so the caption's child
			// blocks render dimmed rather than as body prose.
			in_caption = true;
```

Add an `in_caption` flag to the render loop's local state, set when a `caption` block is seen and cleared when a block at or above the caption's `level` follows. When `in_caption` is set, wrap the produced lines in the theme's dim SGR pair — reuse the same helper `RenderBlockquote` uses for its styling rather than emitting escape codes inline.

- [ ] **Step 4: Build, format, test**

```bash
make && make format-fix && make format-check && make test
```
Expected: PASS.

- [ ] **Step 5: Verify by eye**

```bash
printf '![A **bold** caption](img.png)\n\nOrdinary prose.\n' > /tmp/fig2.md
pandoc -f markdown -t json /tmp/fig2.md > /tmp/fig2.json
./build/release/duckdb -c "SELECT db_blocks_render_ansi(pandoc_ast_to_blocks((SELECT content FROM read_text('/tmp/fig2.json'))));"
```
Expected: the caption is visibly dimmer than "Ordinary prose."

- [ ] **Step 6: Commit**

```bash
git add src/render_ansi.cpp test/sql/render_ansi.test
git commit -m "feat(render): render figure containers and style captions distinctly"
```

---

### Task 8: Emit a pandoc-api-version pandoc 3.x accepts

`duck_blocks_to_pandoc_ast()` hardcodes `[1,20]`. Verified: pandoc 3.1.3 rejects it with "Incompatible API versions: encoded with [1,20] but attempted to decode with [1,23,1]". Every export this extension produces is currently unreadable by the installed pandoc. duckeye works around it in shell (`duckeye:272-278`); this task removes the need.

**Files:**
- Modify: `src/pandoc_block_convert.cpp:1038`, `:1146`, `:1267`
- Test: `test/sql/pandoc_blocks_v2.test`

**Interfaces:**
- Produces: `duck_blocks_to_pandoc_ast(blocks)` defaults to `[1,23,1]`; new optional named argument `api_version := [1,23,1]` (a `LIST(INTEGER)`) overrides it.

- [ ] **Step 1: Write the failing tests**

```
# ============================================================================
# pandoc-api-version
# ============================================================================

query I
SELECT duck_blocks_to_pandoc_ast(db_paragraph([db_text('hi')]))['pandoc-api-version'];
----
[1, 23, 1]

query I
SELECT duck_blocks_to_pandoc_ast(db_paragraph([db_text('hi')]), api_version := [1,23])['pandoc-api-version'];
----
[1, 23]
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `make test 2>&1 | grep -A5 pandoc_blocks_v2`
Expected: FAIL — first returns `[1, 20]`; second errors on the unknown argument.

- [ ] **Step 3: Change the defaults**

In `src/pandoc_block_convert.cpp`, update all three hardcoded sites:

```cpp
// :1038
vector<Value> api_version_vals = {Value::INTEGER(1), Value::INTEGER(23), Value::INTEGER(1)};
// :1146
vector<int32_t> api_version = {1, 23, 1};
// :1267
string api_version = "[1,23,1]";
```

- [ ] **Step 4: Add the optional argument**

At the `duck_blocks_to_pandoc_ast` registration (`:1318`), add a second overload taking `LIST(INTEGER)` as a named argument, following how the existing `pandoc_ast` table function already accepts `api_version := [1,20]` at `:1330` — that code path already parses the list into `bind_data.api_version` (`:1216-1222`) and is the pattern to mirror.

- [ ] **Step 5: Build, format, test**

```bash
make && make format-fix && make format-check && make test
```
Expected: both assertions PASS.

- [ ] **Step 6: Verify real pandoc accepts the output**

```bash
./build/release/duckdb -noheader -list -c "SELECT to_json(duck_blocks_to_pandoc_ast(parse_markdown_to_duck_blocks('# T

Hello *world*.')));" 2>/dev/null | pandoc -f json -t plain
```
Expected: prints the rendered text. Any "Incompatible API versions" error means the task is not done.

- [ ] **Step 7: Commit**

```bash
git add src/pandoc_block_convert.cpp test/sql/pandoc_blocks_v2.test
git commit -m "fix(pandoc): default api-version to [1,23,1] and allow override"
```

---

### Task 9: Alignment harness and spec reconciliation

Locks the gains in: a real-pandoc harness that fails when the handled set drifts, plus the doc corrections. `docs/pandoc_ast_spec.md:422-424` currently advertises `pandoc:lineblock`, `pandoc:deflist` and `pandoc:figure` as mapped — names no code produced before this plan, and which Task 4-6 supersede with `lineblock`, `deflist` and `figure`.

**Files:**
- Create: `test/pandoc/check_pandoc_alignment.py` (copied)
- Create: `test/pandoc/fixtures/` (copied)
- Modify: `docs/pandoc_ast_spec.md:422-424`

**Interfaces:**
- Consumes: every constructor mapped in Tasks 1-6.

- [ ] **Step 1: Copy the harness**

```bash
mkdir -p test/pandoc
cp -r /home/teague/Projects/duckdb_panduck/test/pandoc/check_pandoc_alignment.py test/pandoc/
cp -r /home/teague/Projects/duckdb_panduck/test/pandoc/fixtures test/pandoc/
```

Copied rather than depended on: panduck is itself a consumer of this extension, so a test-time dependency would create a cycle.

- [ ] **Step 2: Retarget it at this extension**

Read `test/pandoc/check_pandoc_alignment.py` in full. It currently queries `panduck_pandoc_ast_map()` for the handled set. Change that to derive the handled set from this extension — run each constructor's minimal JSON through `pandoc_ast_to_blocks()` via `build/release/duckdb` and treat a `generic` result as unhandled. Keep its existing behaviour of skipping cleanly (exit 0) when `pandoc` is not on `$PATH`.

- [ ] **Step 3: Make the gap list a ratchet, not a comment**

Keep the handled/unhandled set in an explicit **ledger** in the script — a literal set of
known-unhandled constructors — and fail in **both** directions:

- a constructor pandoc emits that is unhandled and **not** in the ledger → fail (a new gap appeared)
- a constructor in the ledger that is now **handled** → fail, demanding promotion (the ledger went stale)

Without the second direction the ledger rots: a fixed gap keeps claiming to be broken, and
nobody notices until it misleads someone. panduck's `make test_roundtrip` uses this shape;
it is worth copying deliberately rather than writing a one-directional check.

After Tasks 1-6 the ledger should be **empty**.

- [ ] **Step 4: Do not treat pandoc as ground truth**

Scope this harness to **constructor coverage** — "does pandoc emit a `t` we do not map" —
and not to output equality. Pandoc has real bugs, and asserting agreement invites false
failures. Two confirmed by the panduck session against pandoc 3.1.3: its RTF reader drops
the space after `舒`, and on LibreOffice-produced RTF it reads headings as
`Para[Strong[Span]]`, detecting no heading at all where a stylesheet-aware reader resolves
one correctly.

If a later phase does compare output, it needs the three-way triage panduck already uses —
**we-are-wrong** (fails) / **reference-is-wrong** (recorded, does not fail) /
**not-implemented** (ledgered) — not a plain diff.

- [ ] **Step 5: Run it**

Run: `python3 test/pandoc/check_pandoc_alignment.py`
Expected: reports zero unhandled constructors and an empty ledger. `Figure`, `LineBlock`, `DefinitionList` and `Underline` must no longer appear as gaps.

- [ ] **Step 6: Fix the spec table**

In `docs/pandoc_ast_spec.md`, replace the three rows at `:422-424`:

```markdown
| `LineBlock` | `lineblock` | NULL | text | Lines joined with `\n` |
| `DefinitionList` | `deflist` | NULL | json | Full structure preserved |
| `Figure` | `figure` | depth | text | Pandoc 3.0+; content blocks then a `caption` container |
```

and add rows for the new types:

```markdown
| `Underline` | `underline` | NULL | text | Inline; round-trips both directions |
| *(unrecognised)* | `generic` | NULL | json | Verbatim constructor; `pandoc_type` in attrs |
```

- [ ] **Step 7: Document the caption container**

Add a short subsection to `docs/pandoc_ast_spec.md` under the block table showing the `figure`/`caption` layout, using the diagram from the spec's §1. State explicitly that content blocks precede the caption container, and that `caption` is defined generally so `Table` can adopt it later.

- [ ] **Step 8: Commit**

```bash
git add test/pandoc docs/pandoc_ast_spec.md
git commit -m "test(pandoc): add real-pandoc alignment harness; docs: reconcile AST spec"
```

---

## Phase 1 Done When

- `make test` passes.
- `python3 test/pandoc/check_pandoc_alignment.py` reports no unhandled constructors.
- The original repro yields four duck_blocks, not one:
  ```bash
  ./build/release/duckdb -c "SELECT b.element_type FROM (SELECT unnest(pandoc_ast_to_blocks((SELECT content FROM read_text('/tmp/gaps.json')))) AS b) t;"
  ```
- Output round-trips through real pandoc without an api-version error.

Phase 2 (dispatcher) is planned separately in `2026-08-31-reader-dispatch.md` and does not depend on this phase beyond a green build.
