#!/usr/bin/env python3
"""Do the fleet's producers agree with the spec, on the same document?

Every other check here compares THIS repo with itself. The divergences consumers hit
are between producers (issue #29): markdown started element_order at 1, and emitted
the loose list-item shape for a tight list, while webbed and panduck did not. Nothing
in any repo's suite could see that, because each suite verifies its own behaviour.

This reads each corpus document with every installed producer and compares the
emitted sequence -- after duck_blocks_repair and duck_blocks_normalize, on
kind / element_type / level / content and the attribute keys the spec names for the
type -- against the expected sequence the spec implies, written by hand and reviewed.

ATTRIBUTION: a difference is reported as "producer X moved from v to v'" (its
installed version differs from what the manifest recorded when expected was written)
or "disagrees with expected (written against ...)", never as a bare difference: a bare
difference gets attributed by elimination to whichever producer nobody probed
(duckeye's finding, 2026-09-10).

THIS IS A TEST, NOT A FILTER. It runs after a producer builds and after a consumer's
corpus builds; an eleven-hour build finishes before any test says it was wrong. It
does not replace a consumer filtering fail-safe on its own (spec: "Consumers must
filter on kind").

Measures the INSTALLED community build of each producer in a clean extension
directory: what users get, not a working tree. Skips (never OK) when the duckdb CLI or
a producer is not installable here -- absence is not conformance.

  python3 test/check_conformance_corpus.py                  # check
  python3 test/check_conformance_corpus.py --write-expected markdown
                                                             # regenerate expected from a
                                                             # producer; REVIEW before commit
"""

import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE / "conformance"
REPO = HERE.parent
COMPARE_KEYS = ("kind", "element_type", "level", "content")


def load_manifest():
    import yaml

    return yaml.safe_load((ROOT / "manifest.yaml").read_text())


# duck_block_utils itself: this repo's own build when it exists (the rules under test
# may be newer than the registry serves), else the community build. Producers always
# come from the registry, because what users get is what is being measured.
LOCAL_UTILS = REPO / "build" / "release" / "extension" / "duck_block_utils" / "duck_block_utils.duckdb_extension"


def utils_load_sql():
    if LOCAL_UTILS.exists():
        return f"LOAD '{LOCAL_UTILS}';"
    return "INSTALL duck_block_utils FROM community; LOAD duck_block_utils;"


def run_sql(cli, extdir, sql):
    cmd = [cli, "-unsigned", "-json", "-c", f"SET extension_directory='{extdir}'; {sql}"]
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode != 0:
        raise RuntimeError(p.stderr.strip())
    return json.loads(p.stdout or "[]")


def producer_version(cli, extdir, name, install):
    rows = run_sql(
        cli, extdir, f"{install} SELECT extension_version AS v FROM duckdb_extensions() WHERE extension_name='{name}';"
    )
    return rows[0]["v"] if rows else None


def emitted(cli, extdir, install, read, path):
    sql = (
        f"{install} {utils_load_sql()} WITH src AS ({read.format(path=path)}) "
        "SELECT b.kind, b.element_type, b.level, coalesce(b.content, '') AS content, "
        "to_json(b.attributes)::VARCHAR AS attributes "
        "FROM (SELECT unnest(duck_blocks_repair(duck_blocks_normalize(list(b)))) AS b FROM src);"
    )
    return run_sql(cli, extdir, sql)


def raw_list_errors(cli, extdir, install, read, path):
    """The list-level rules on the producer's RAW output, before repair.

    The structural comparison repairs first, so that one element_order off-by-one does
    not redden every construct -- which also means a green structural line says
    nothing about order (markdown#59 was real and invisible to it; markdown session,
    2026-09-10). So the rules are checked separately here and reported as their own
    failure, with the producer named.
    """
    sql = (
        f"{install} {utils_load_sql()} WITH src AS ({read.format(path=path)}) "
        "SELECT e.message AS m FROM (SELECT unnest(duck_blocks_validate(list(b)).errors) AS e FROM src) "
        "WHERE e.field = 'list';"
    )
    return [r["m"] for r in run_sql(cli, extdir, sql)]


def merge_inline_runs(rows):
    """Adjacent inline `text` / `space` siblings at one level become one `text`.

    Pandoc-derived readers emit `Str`/`Space` tokens; cmark-derived readers emit one
    text with the spaces inside. The spec does not rule on tokenisation, and
    duck_blocks_to_text already treats the two as the same run, so the corpus
    compares runs, not tokens. A `space` contributes " " whatever its content.
    """
    out = []
    for r in rows:
        is_ws = r["kind"] == "inline" and r["element_type"] in ("text", "space", "softbreak")
        if (
            is_ws
            and out
            and out[-1]["kind"] == "inline"
            and out[-1]["element_type"] == "text"
            and out[-1]["level"] == r["level"]
        ):
            out[-1]["content"] += " " if r["element_type"] in ("space", "softbreak") else r["content"]
            continue
        if is_ws and r["element_type"] != "text":
            r = dict(r, element_type="text", content=" ")
        out.append(dict(r))
    return out


def normalise(rows):
    out = []
    for r in rows:
        attrs = r.get("attributes") or {}
        if isinstance(attrs, str):
            attrs = json.loads(attrs) if attrs else {}
        out.append(
            {
                "kind": r["kind"],
                "element_type": r["element_type"],
                "level": int(r["level"]),
                "content": r.get("content") or "",
                "attributes": {k: str(v) for k, v in (attrs or {}).items()},
            }
        )
    return merge_inline_runs(out)


def first_difference(exp, got):
    for i, (e, g) in enumerate(zip(exp, got)):
        for k in COMPARE_KEYS:
            if e[k] != g[k]:
                return i, k, e[k], g[k]
        for k, v in e["attributes"].items():
            if g["attributes"].get(k) != v:
                return i, f"attributes[{k}]", v, g["attributes"].get(k)
    if len(exp) != len(got):
        return min(len(exp), len(got)), "length", len(exp), len(got)
    return None


def main():
    cli = shutil.which("duckdb")
    if not cli:
        print("SKIP: no duckdb CLI on PATH (absence is not conformance)")
        return 0
    m = load_manifest()
    write_for = sys.argv[2] if len(sys.argv) == 3 and sys.argv[1] == "--write-expected" else None
    failed = False
    with tempfile.TemporaryDirectory() as extdir:
        try:
            utils = run_sql(cli, extdir, f"{utils_load_sql()} SELECT duck_block_spec_version() AS v;")[0]["v"]
        except RuntimeError as e:
            print(f"SKIP: cannot load duck_block_utils here: {str(e)[:100]}")
            return 0
        print(f"duck_block_utils spec {utils} ({'local build' if LOCAL_UTILS.exists() else 'community'})")
        print("  structure is compared after duck_blocks_repair; the list rules (element_order from 0, levels)")
        print("  are checked on each producer's RAW output and reported separately")
        declared_kinds = {
            r["k"] for r in run_sql(cli, extdir, f"{utils_load_sql()} SELECT unnest(duck_block_kind_names()) AS k;")
        }
        declared_types = {
            r["t"] for r in run_sql(cli, extdir, f"{utils_load_sql()} SELECT unnest(duck_block_type_names()) AS t;")
        }
        for name, p in m["producers"].items():
            try:
                ver = producer_version(cli, extdir, name, p["install"])
            except RuntimeError as e:
                print(f"SKIP {name}: not installable here ({str(e)[:80]})")
                continue
            if not ver:
                print(f"SKIP {name}: not installed (absence is not conformance)")
                continue
            print(f"Checking {name} {ver}")
            kinds, types = set(), set()
            order_failures = {}  # list-rule message -> constructs it appeared on (raw output)
            for cdir in sorted(ROOT.joinpath("corpus").iterdir()):
                src = next((cdir / f"source.{f}" for f in p["formats"] if (cdir / f"source.{f}").exists()), None)
                if not src:
                    continue
                try:
                    got = normalise(emitted(cli, extdir, p["install"], p["read"], src))
                except RuntimeError as e:
                    failed = True
                    print(f"  FAIL {cdir.name}: {name} {ver} could not read it: {str(e)[:120]}")
                    continue
                kinds |= {g["kind"] for g in got}
                types |= {g["element_type"] for g in got}
                try:
                    for msg in raw_list_errors(cli, extdir, p["install"], p["read"], src):
                        order_failures.setdefault(msg, []).append(cdir.name)
                except RuntimeError:
                    pass
                exp_path = cdir / "expected.blocks.json"
                if write_for == name:
                    exp_path.write_text(json.dumps(got, indent=2) + "\n")
                    m.setdefault("written_against", {}).setdefault(cdir.name, {})[name] = ver
                    print(f"  wrote {exp_path.relative_to(REPO)} from {name} {ver}")
                    continue
                exp = json.loads(exp_path.read_text())
                d = first_difference(exp, got)
                if d is None:
                    print(f"  OK   {cdir.name}")
                    continue
                failed = True
                recorded = (m.get("written_against") or {}).get(cdir.name, {}).get(name)
                who = (
                    f"{name} moved from {recorded} to {ver}"
                    if recorded and recorded != ver
                    else f"{name} {ver} disagrees with expected (written against {recorded or 'the spec, by hand'})"
                )
                i, k, e, g = d
                print(f"  FAIL {cdir.name}: {who}; element {i} {k}: expected {e!r}, got {g!r}")
            # One line per producer per rule, not one per construct: a 1-origin is one
            # defect however many documents show it.
            if order_failures and write_for != name:
                failed = True
                for msg, constructs in sorted(order_failures.items()):
                    print(f"  FAIL {name} {ver} raw output breaks a list rule on {len(constructs)} construct(s): {msg}")
            # Closed-set assertions: a new kind or element_type fails at the producer that
            # introduced it, not as a silently skipped element in a consumer's allowlist.
            for label, got_set, decl in (("kind", kinds, declared_kinds), ("element_type", types, declared_types)):
                extra = sorted(got_set - decl)
                if extra:
                    failed = True
                    print(f"  FAIL {name} {ver} emits undeclared {label} values {extra}")
    if write_for:
        import yaml

        (ROOT / "manifest.yaml").write_text(yaml.safe_dump(m, sort_keys=False))
        print("manifest updated; REVIEW the expected files against the spec before committing them")
        return 0
    print("FAIL: producers disagree with the spec" if failed else "OK: every installed producer agrees with the corpus")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
