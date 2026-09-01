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
check:
	python3 test/check_vocabulary_header.py
	python3 test/check_spec_alignment.py
	python3 test/pandoc/check_pandoc_alignment.py

check-strict:
	DUCK_BLOCK_CHECKS_STRICT=1 $(MAKE) check
