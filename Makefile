PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Configuration of extension
EXT_NAME=duck_block_utils
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

# Include the Makefile from extension-ci-tools
include extension-ci-tools/makefiles/duckdb_extension.Makefile
# The vocabulary/spec/pandoc ratchets. These are guards other extensions were
# told to rely on, so they need a way to run that is not "someone remembers to
# type it". STRICT=1 turns a skipped check into a failed one -- see the `skip()`
# helper in each script for why that distinction matters.
.PHONY: check check-strict
# EVERY check runs, even after one fails. They are independent and were sequenced,
# so `make` stopped at the first non-zero exit and the rest never executed -- a
# failure in check 2 meant checks 3-8 went unverified, and the output looked like an
# ordinary failure rather than "and six things were not looked at".
#
# Found by the panduck session hitting the same shape in their test harness: a real
# defect sat behind an earlier failure in the same file, so a test that existed for
# it never ran and never reported. A suite that stopped early and a suite that passed
# are indistinguishable from the summary line.
#
# HOW TO READ A CHECK YOU ARE PERTURBING. Every "watched it fail" note in test/ says to
# re-plant the perturbation and confirm the red; none of them said how to LOOK, and that
# omission is where three false readings came from in one hour on 2026-09-01 -- across
# two repos, on the evening both were arguing that an ad-hoc measurement is wrong the
# same way a check is.
#
#   1. Run the script UNPIPED. `$?` and `${PIPESTATUS[0]}` after a pipe report the last
#      command's status, so `python3 check_x.py | tail -5; echo $?` prints tail's 0 while
#      the check exits 1.
#   2. ONE input at a time. A glob that expands to two paths hands the second as a
#      nonsense argument, and the error names neither.
#   3. WHOLE output. `| tail -n` truncates the error above the line you are reading, and
#      a truncated print is still a print.
#   4. CONFIRM THE PERTURBED STATE EXISTS before believing the result -- print the
#      mutated region, never a count of it. `grep -c` counts LINES, so it reports a match
#      for text that was already there and your edit never landed.
#
# THE LIMIT, from duckdb_markdown, who put this rule in their own build first: it kills
# the MECHANICAL misreadings and not the interpretive one. You can run the right command,
# unpiped, on one input, and still read the wrong three lines of correct output. That
# residue is the class no rule closes -- the one that has needed a second person all
# along.
check:
	@fail=0; \
	for c in test/check_vocabulary_header.py \
	         test/check_spec_alignment.py \
	         test/check_render_sweep.py \
	         test/check_conformance_macro.py \
	         test/check_vendorable.py \
	         test/check_docs_cover_functions.py \
	         test/check_constants_are_used.py \
	         test/check_consumer_alignment.py \
	         test/check_conformance_corpus.py; do \
	  python3 $$c || { fail=1; echo "  ^^ $$c FAILED (continuing; the rest still run)"; }; \
	done; \
	if [ $$fail -ne 0 ]; then echo; echo "One or more checks failed. All of them ran."; exit 1; fi

check-strict:
	DUCK_BLOCK_CHECKS_STRICT=1 $(MAKE) check

.PHONY: test-require
# A TEST RUN THAT SKIPPED REPORTS SUCCESS, which is how 680 tests across this fleet
# went unrun behind 12 green registry checks in October 2026
# (duckdb/community-extensions#3086). `require duck_block_utils` in a .test file means
# "skip unless available", so when the extension cannot be loaded EVERY file skips and
# the harness still prints "all tests passed".
#
# `--require <ext>` makes a MISSING require a hard failure instead: see
# sqllogic_test_runner.cpp, where IsRequired(param) turns it into
# parser.Fail("require %s: FAILED"). It is SCOPED to the named extension, so files that
# skip for other reasons still skip. That scoping is why this is used rather than
# run_tests.py's --fail-require-skip, which cannot tell an unloadable extension from a
# deliberate platform gate -- and which is a no-op anyway, because it searches stdout
# while the summary is written to stderr (duckdb/duckdb#27413).
#
# WATCHED IT FAIL BOTH WAYS before being trusted, on DuckDB 2.0 (b15358c371d), by moving
# the built extension aside:
#     make test-require, present : exit 0, "2 skipped tests, 813 assertions in 16 cases"
#     make test-require, hidden  : exit 2, "require duck_block_utils: FAILED"
#     make test     , hidden     : exit 0, "ran tests: 0 passed, 18 skipped"   <-- the bug
# The present-case's 2 legitimate skips passing is the proof that the scoping works.
#
# THE NAME IS WRITTEN OUT, not $(EXT_NAME), because the two are not always the same:
# sitting_duck sets EXT_NAME=duckdb_ast while its tests say `require sitting_duck`, so
# $(EXT_NAME) there would match no token and make this guard a silent no-op -- the exact
# failure class it exists to catch. Found while rolling this out across the fleet; here
# the two happen to coincide, which is precisely why relying on it is a trap.
#
# INERT ON THE v1.5.6 LINE, where `require` cannot fail: hiding the artifact changes
# nothing (measured on markdown and yaml -- identical assertion counts all three ways).
# Harmless there, load-bearing on the 2.0 line. Report the ASSERTION count this prints,
# never `make test`'s file count.
test-require:
	./build/release/test/unittest --require duck_block_utils "$(TESTS_BASE_DIRECTORY)*"
