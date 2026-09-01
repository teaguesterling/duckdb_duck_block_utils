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
check:
	@fail=0; \
	for c in test/check_vocabulary_header.py \
	         test/check_spec_alignment.py \
	         test/pandoc/check_pandoc_alignment.py \
	         test/check_roundtrip_sweep.py \
	         test/check_conformance_macro.py \
	         test/check_vendorable.py \
	         test/check_docs_cover_functions.py \
	         test/check_constants_are_used.py \
	         test/check_consumer_alignment.py \
	         test/fixtures/metadata/check_metadata_fixtures.py; do \
	  python3 $$c || { fail=1; echo "  ^^ $$c FAILED (continuing; the rest still run)"; }; \
	done; \
	if [ $$fail -ne 0 ]; then echo; echo "One or more checks failed. All of them ran."; exit 1; fi

check-strict:
	DUCK_BLOCK_CHECKS_STRICT=1 $(MAKE) check
