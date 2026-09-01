# Regenerate the pandoc ASTs from the sources. Pinned at pandoc 3.1.3: a differing AST
# is a finding about pandoc, to be read before expectations are updated to match it.
ast:
	pandoc -f markdown -t json src/frontmatter_and_body.md > ast/frontmatter_and_body.json
	pandoc -f markdown -t json src/empty_field_yaml.md     > ast/empty_field_yaml.json
	pandoc -f org      -t json src/empty_field_org.org     > ast/empty_field_org.json
	pandoc -f org      -t json src/multi_author_concat.org > ast/multi_author_concat.json
	pandoc -f latex    -t json src/multi_author_list.tex   > ast/multi_author_list.json
	pandoc -f markdown -t json src/value_kinds.md          > ast/value_kinds.json
.PHONY: ast
