# Conformance corpus

One directory per construct the spec names, with the same document in every source
format a fleet producer reads (`source.md`, `source.html`, ...) and the sequence of
duck_blocks the spec implies for it (`expected.blocks.json`), written by hand from the
spec and reviewed, not generated from a producer.

`python3 test/check_conformance_corpus.py` reads every construct with every producer
installed from the community registry (markdown, webbed, panduck) in a clean
extension directory, so it measures what users get, and compares the emitted
sequence against `expected` after `duck_blocks_repair` and `duck_blocks_normalize`,
on `kind`, `element_type`, `level`, `content` and the attribute keys `expected`
names. Adjacent inline `text`/`space` siblings are merged before comparing: the spec
does not rule on tokenisation and `duck_blocks_to_text` treats the two as one run.
`duck_block_utils` itself is this repo's build when one exists, so the rules under
test can be newer than the registry serves.

**What a failure says.** Never a bare difference. Each line names the construct, the
producer and its installed version, and the first differing element:

    FAIL tight_list: markdown 75e9d0b disagrees with expected (written against the
    spec, by hand); element 1 content: expected 'alpha', got ''

or, when the manifest recorded the producer's version at the time `expected` was
last written, `markdown moved from X to Y`. A difference reported without a producer
gets attributed by elimination to whichever producer nobody probed, which is how a
panduck change was once blamed on a sibling (duckeye, 2026-09-10).

**Closed-set assertions.** Every producer's emitted `kind` and `element_type` values
must be in `duck_block_kind_names()` / `duck_block_type_names()`, so a new value
fails at the producer that introduced it rather than as a silently skipped element in
a consumer's allowlist.

**This is a test, not a filter.** It runs after a producer builds and after a
consumer's corpus builds; an eleven-hour build finishes before any test says it was
wrong. It does not replace a consumer filtering fail-safe on its own (spec:
"Consumers must filter on `kind`").

**Negative control, run before trusting a green.** Corrupt one passing expectation
(e.g. `paragraph`'s content), run, and confirm a `FAIL paragraph: <producer> ...
expected 'WRONG', got 'Just one paragraph.'` line per producer; restore it. Verified
2026-09-10 against markdown 75e9d0b, webbed 73189d2, panduck c8aee8a.

**Adding a construct.** A directory with `source.md` (and `source.html`, hand-written
where pandoc's HTML writer adds wrappers, e.g. code), `expected.blocks.json` derived
from the spec, and a check of that expectation against this repo's own pandoc reader
(`pandoc -t json | pandoc_ast_to_blocks`) as an oracle before committing.
`--write-expected <producer>` regenerates every `expected` from one producer and
records its version in `manifest.yaml`; review the result against the spec before
committing, because a producer's output is what is being judged, not the judge.

**Known red on 2026-09-10, all real:** markdown (and panduck's `.md` path, which
delegates to it) collapses a block child onto the container's `content` for tight
list items, nested tight items and blockquotes (markdown#60 family); webbed keeps
HTML source whitespace (a trailing newline) inside text nodes (`nested_list`).
