#!/bin/bash
# Generate expected outputs for all Pandoc formats from duck_block fixtures
#
# Usage: ./generate_expectations.sh
#
# Requires: pandoc, duckdb with duck_block_utils extension

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DUCKDB="${SCRIPT_DIR}/../build/release/duckdb"

# Output directory for generated expectations
OUTPUT_DIR="${SCRIPT_DIR}/expected"
mkdir -p "$OUTPUT_DIR"

# Pandoc text-based output formats to generate
FORMATS=(
    "markdown"
    "gfm"           # GitHub-flavored markdown
    "html"
    "latex"
    "rst"           # reStructuredText
    "asciidoc"
    "org"           # Org-mode
    "mediawiki"
    "jira"
    "textile"
    "typst"
    "plain"         # Plain text
)

# Get list of fixture names from all tables
get_fixtures() {
    cat << EOF | "$DUCKDB" -noheader -list
.read ${SCRIPT_DIR}/sql/01_block_types.sql
.read ${SCRIPT_DIR}/sql/02_lists.sql
.read ${SCRIPT_DIR}/sql/03_documents.sql
SELECT 'block_type:' || name FROM block_type_fixtures
UNION ALL SELECT 'list:' || name FROM list_fixtures
UNION ALL SELECT 'document:' || name FROM document_fixtures;
EOF
}

# Generate Pandoc JSON for a specific fixture
generate_json() {
    local category="$1"
    local name="$2"

    cat << EOF | "$DUCKDB" -json -noheader 2>/dev/null | jq -c '.[0].doc'
LOAD json;
.read ${SCRIPT_DIR}/sql/01_block_types.sql
.read ${SCRIPT_DIR}/sql/02_lists.sql
.read ${SCRIPT_DIR}/sql/03_documents.sql
SELECT
    json_object(
        'pandoc-api-version', [1, 20]::INT[],
        'meta', '{}'::JSON,
        'blocks', duck_blocks_to_pandoc_blocks(blocks)::JSON
    ) as doc
FROM ${category}_fixtures
WHERE name = '${name}';
EOF
}

# Generate all format outputs for a fixture
generate_fixture_outputs() {
    local category="$1"
    local name="$2"

    echo "Processing: $category/$name"

    # Create output subdirectory
    local fixture_dir="$OUTPUT_DIR/$category"
    mkdir -p "$fixture_dir"

    # Generate and save the Pandoc JSON
    local json
    if json=$(generate_json "$category" "$name") && [ -n "$json" ]; then
        echo "$json" > "$fixture_dir/${name}.json"
        echo "  Generated: json"
    else
        echo "  Failed: json"
        return
    fi

    # Generate each format from the JSON
    for format in "${FORMATS[@]}"; do
        local outfile="$fixture_dir/${name}.${format}"
        if echo "$json" | pandoc -f json -t "$format" > "$outfile" 2>/dev/null; then
            echo "  Generated: $format"
        else
            echo "  Failed: $format"
            rm -f "$outfile"
        fi
    done
}

# Main
echo "Generating Pandoc format expectations..."
echo "Output directory: $OUTPUT_DIR"
echo ""

# Process each fixture
while IFS=: read -r category name; do
    generate_fixture_outputs "$category" "$name"
done < <(get_fixtures)

echo ""
echo "Done. Generated files in: $OUTPUT_DIR"

# Generate summary
echo ""
echo "Summary:"
for format in "json" "${FORMATS[@]}"; do
    count=$(find "$OUTPUT_DIR" -name "*.${format}" 2>/dev/null | wc -l)
    echo "  $format: $count files"
done
