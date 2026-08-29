#include "render_ansi.hpp"
#include "block_types.hpp"
#include "duckdb/common/types/value.hpp"
#include "utf8proc_wrapper.hpp"

#include <cstdlib>

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace duckdb {

namespace {

constexpr idx_t DEFAULT_WIDTH = 80;
constexpr idx_t MIN_WIDTH = 10;
constexpr idx_t MIN_TABLE_COL = 5;

const char *const ESC = "\x1b[";
const char *const RESET = "\x1b[0m";

// Theme palettes (ANSI 256-color codes for dark and light backgrounds)
struct ThemePalette {
	const char *h1;
	const char *h2;
	const char *h3;
	const char *code_inline;
	const char *code_text;
	const char *code_lang;
	const char *link;
	const char *bullet;
	const char *quote;
	const char *dim;
	const char *bold_hdr;
};

// Dark theme: vibrant high contrast for dark terminal backgrounds
const ThemePalette THEME_DARK = {
    "1;38;5;219", // H1: bubblegum pink bold
    "1;38;5;141", // H2: lavender bold
    "1;38;5;75",  // H3: sky blue bold
    "38;5;203",   // code_inline: salmon red
    "38;5;252",   // code_text: light grey
    "38;5;222",   // code_lang: pale gold
    "4;38;5;75",  // link: underlined sky blue
    "38;5;212",   // bullet: hot pink
    "3;38;5;115", // quote: mint italic
    "2",          // dim
    "1"           // bold_hdr
};

// Light theme: deeper, saturated high contrast for white/light terminal backgrounds
const ThemePalette THEME_LIGHT = {
    "1;38;5;125", // H1: deep ruby bold
    "1;38;5;55",  // H2: deep purple bold
    "1;38;5;25",  // H3: deep navy bold
    "38;5;160",   // code_inline: crimson
    "38;5;236",   // code_text: dark charcoal
    "38;5;130",   // code_lang: amber/sienna
    "4;38;5;27",  // link: underlined royal blue
    "38;5;161",   // bullet: deep magenta
    "3;38;5;28",  // quote: forest green italic
    "2",          // dim
    "1"           // bold_hdr
};

static const ThemePalette &ResolveTheme(const string &theme_name = "") {
	string name = theme_name;
	if (name.empty() || name == "auto") {
		const char *env_theme = std::getenv("DUCK_BLOCK_THEME");
		if (!env_theme) {
			env_theme = std::getenv("DUCKEYE_THEME");
		}
		if (env_theme) {
			name = env_theme;
		} else {
			const char *colorfgbg = std::getenv("COLORFGBG");
			if (colorfgbg) {
				string cfg(colorfgbg);
				auto pos = cfg.rfind(';');
				if (pos != string::npos) {
					int bg = std::atoi(cfg.substr(pos + 1).c_str());
					if (bg == 7 || bg == 15) {
						name = "light";
					}
				}
			}
		}
	}
	string lower;
	for (char c : name) {
		lower += (char)std::tolower(c);
	}
	if (lower == "light") {
		return THEME_LIGHT;
	}
	return THEME_DARK;
}

static string Sgr(const string &params) {
	return string(ESC) + params + "m";
}

// ---------------------------------------------------------------------------
// SGR state tracking: enough to re-open active styles after a line break.
// ---------------------------------------------------------------------------
struct SgrState {
	bool bold = false;
	bool dim = false;
	bool italic = false;
	bool underline = false;
	bool strike = false;
	string fg; // full param string, e.g. "38;5;219" or "31"
	string bg;

	void Reset() {
		*this = SgrState();
	}

	// Apply one SGR sequence's parameter string (e.g. "1;38;5;219")
	void Apply(const string &params) {
		vector<string> parts;
		string cur;
		for (char c : params) {
			if (c == ';') {
				parts.push_back(cur);
				cur.clear();
			} else {
				cur += c;
			}
		}
		parts.push_back(cur);

		for (idx_t i = 0; i < parts.size(); i++) {
			const string &p = parts[i];
			int code = p.empty() ? 0 : atoi(p.c_str());
			switch (code) {
			case 0:
				Reset();
				break;
			case 1:
				bold = true;
				break;
			case 2:
				dim = true;
				break;
			case 3:
				italic = true;
				break;
			case 4:
				underline = true;
				break;
			case 9:
				strike = true;
				break;
			case 22:
				bold = dim = false;
				break;
			case 23:
				italic = false;
				break;
			case 24:
				underline = false;
				break;
			case 29:
				strike = false;
				break;
			case 39:
				fg.clear();
				break;
			case 49:
				bg.clear();
				break;
			case 38:
			case 48: {
				// extended color: 38;5;N or 38;2;R;G;B
				string full = p;
				idx_t consume = 0;
				if (i + 1 < parts.size() && parts[i + 1] == "5") {
					consume = 2;
				} else if (i + 1 < parts.size() && parts[i + 1] == "2") {
					consume = 4;
				}
				for (idx_t j = 1; j <= consume && i + j < parts.size(); j++) {
					full += ";" + parts[i + j];
				}
				i += consume;
				if (code == 38) {
					fg = full;
				} else {
					bg = full;
				}
				break;
			}
			default:
				if ((code >= 30 && code <= 37) || (code >= 90 && code <= 97)) {
					fg = p;
				} else if ((code >= 40 && code <= 47) || (code >= 100 && code <= 107)) {
					bg = p;
				}
				// other codes ignored
				break;
			}
		}
	}

	bool Any() const {
		return bold || dim || italic || underline || strike || !fg.empty() || !bg.empty();
	}

	// Escape sequence re-establishing this state from a clean slate
	string Open() const {
		if (!Any()) {
			return "";
		}
		string params;
		auto add = [&](const string &p) {
			if (!params.empty()) {
				params += ";";
			}
			params += p;
		};
		if (bold) {
			add("1");
		}
		if (dim) {
			add("2");
		}
		if (italic) {
			add("3");
		}
		if (underline) {
			add("4");
		}
		if (strike) {
			add("9");
		}
		if (!fg.empty()) {
			add(fg);
		}
		if (!bg.empty()) {
			add(bg);
		}
		return Sgr(params);
	}
};

// ---------------------------------------------------------------------------
// Display width measurement: skips SGR escapes, uses utf8proc render width
// (CJK/emoji = 2 columns, combining marks = 0).
// ---------------------------------------------------------------------------

// Advance past an ANSI escape sequence starting at pos ("\x1b..."); returns
// position after it. Only CSI sequences are expected but we skip others safely.
static size_t SkipEscape(const string &s, size_t pos) {
	D_ASSERT(s[pos] == '\x1b');
	pos++;
	if (pos < s.size() && s[pos] == '[') {
		pos++;
		while (pos < s.size() && !(s[pos] >= 0x40 && s[pos] <= 0x7e)) {
			pos++;
		}
		if (pos < s.size()) {
			pos++; // final byte (e.g. 'm')
		}
	}
	return pos;
}

static size_t DisplayWidth(const string &s) {
	size_t width = 0;
	size_t pos = 0;
	while (pos < s.size()) {
		if (s[pos] == '\x1b') {
			pos = SkipEscape(s, pos);
			continue;
		}
		size_t next = Utf8Proc::NextGraphemeCluster(s.c_str(), s.size(), pos);
		width += Utf8Proc::RenderWidth(s.c_str(), s.size(), pos);
		pos = next;
	}
	return width;
}

static string PadTo(const string &s, size_t target) {
	size_t w = DisplayWidth(s);
	string out = s;
	while (w < target) {
		out += ' ';
		w++;
	}
	return out;
}

// ---------------------------------------------------------------------------
// Word wrap of styled text. Tracks SGR state so wrapped lines re-open active
// styles after the (independently styled) continuation prefix.
// ---------------------------------------------------------------------------
struct WrapPrefix {
	string first;       // prefix of the first line (already styled + reset)
	string cont;        // prefix of continuation lines (already styled + reset)
	size_t first_width; // display columns consumed by `first`
	size_t cont_width;  // display columns consumed by `cont`
};

// Split styled text into words. Escapes attach to the word they precede/inhabit.
struct StyledWord {
	string text;      // includes any embedded escapes
	size_t width = 0; // display columns
};

static vector<StyledWord> SplitWords(const string &styled) {
	vector<StyledWord> words;
	StyledWord cur;
	size_t pos = 0;
	auto flush = [&]() {
		if (!cur.text.empty()) {
			words.push_back(cur);
			cur = StyledWord();
		}
	};
	while (pos < styled.size()) {
		if (styled[pos] == '\x1b') {
			size_t end = SkipEscape(styled, pos);
			cur.text += styled.substr(pos, end - pos);
			pos = end;
			continue;
		}
		if (styled[pos] == ' ' || styled[pos] == '\t') {
			flush();
			pos++;
			continue;
		}
		size_t next = Utf8Proc::NextGraphemeCluster(styled.c_str(), styled.size(), pos);
		cur.width += Utf8Proc::RenderWidth(styled.c_str(), styled.size(), pos);
		cur.text += styled.substr(pos, next - pos);
		pos = next;
	}
	flush();
	return words;
}

// Hard-break a single over-long word into width-sized chunks, keeping SGR
// state coherent across the breaks.
static void HardBreak(const StyledWord &word, size_t width, SgrState &state, vector<string> &pieces) {
	string piece = state.Open();
	size_t piece_width = 0;
	size_t pos = 0;
	const string &s = word.text;
	while (pos < s.size()) {
		if (s[pos] == '\x1b') {
			size_t end = SkipEscape(s, pos);
			string esc = s.substr(pos, end - pos);
			piece += esc;
			if (end > pos + 2 && s[end - 1] == 'm') {
				state.Apply(s.substr(pos + 2, end - pos - 3));
			}
			pos = end;
			continue;
		}
		size_t next = Utf8Proc::NextGraphemeCluster(s.c_str(), s.size(), pos);
		size_t cw = Utf8Proc::RenderWidth(s.c_str(), s.size(), pos);
		if (piece_width + cw > width && piece_width > 0) {
			pieces.push_back(piece + (state.Any() ? RESET : ""));
			piece = state.Open();
			piece_width = 0;
		}
		piece += s.substr(pos, next - pos);
		piece_width += cw;
		pos = next;
	}
	pieces.push_back(piece + (state.Any() ? RESET : ""));
}

// Wrap styled text to `width` total columns with per-line prefixes.
// Returns fully assembled lines (prefix + content).
static vector<string> WrapStyled(const string &styled, size_t width, const WrapPrefix &prefix) {
	vector<string> lines;
	auto words = SplitWords(styled);

	SgrState state;
	string line;
	size_t line_width = 0;
	bool first_line = true;
	size_t avail = width > prefix.first_width ? width - prefix.first_width : 1;

	auto begin_line = [&]() {
		line = (first_line ? prefix.first : prefix.cont) + state.Open();
		line_width = 0;
		avail = width - (first_line ? prefix.first_width : prefix.cont_width);
		if (avail < 1) {
			avail = 1;
		}
	};
	auto end_line = [&]() {
		lines.push_back(line + (state.Any() ? RESET : ""));
		first_line = false;
	};

	begin_line();
	for (auto &word : words) {
		// Track state through this word's escapes for future lines
		SgrState state_after = state;
		size_t epos = 0;
		while ((epos = word.text.find('\x1b', epos)) != string::npos) {
			size_t end = SkipEscape(word.text, epos);
			if (end > epos + 2 && word.text[end - 1] == 'm') {
				state_after.Apply(word.text.substr(epos + 2, end - epos - 3));
			}
			epos = end;
		}

		size_t space = line_width > 0 ? 1 : 0;
		if (line_width + space + word.width <= avail) {
			line += (space ? " " : "") + word.text;
			line_width += space + word.width;
			state = state_after;
			continue;
		}
		// Word doesn't fit on this line
		if (word.width > avail) {
			// Hard-break: flush current line first if it has content
			if (line_width > 0) {
				end_line();
				begin_line();
			}
			size_t inner = width - (first_line ? prefix.first_width : prefix.cont_width);
			if (inner < 1) {
				inner = 1;
			}
			vector<string> pieces;
			HardBreak(word, inner, state, pieces);
			for (idx_t p = 0; p < pieces.size(); p++) {
				if (p > 0) {
					end_line();
					begin_line();
				}
				// pieces already carry their own opens/resets
				line += pieces[p];
				line_width = inner; // approximation; last piece may be shorter
			}
			// Recompute width of last piece for continued packing
			line_width = DisplayWidth(line) - (first_line ? prefix.first_width : prefix.cont_width);
			continue;
		}
		end_line();
		begin_line();
		line += word.text;
		line_width = word.width;
		state = state_after;
	}
	end_line();
	return lines;
}

static WrapPrefix MakePrefix(const string &first_raw, const string &cont_raw, const string &style) {
	WrapPrefix p;
	p.first_width = DisplayWidth(first_raw);
	p.cont_width = DisplayWidth(cont_raw);
	if (style.empty()) {
		p.first = first_raw;
		p.cont = cont_raw;
	} else {
		p.first = first_raw.empty() ? "" : Sgr(style) + first_raw + RESET;
		p.cont = cont_raw.empty() ? "" : Sgr(style) + cont_raw + RESET;
	}
	return p;
}

// ---------------------------------------------------------------------------
// Minimal JSON parser for machine-generated block content
// (lists: ["a", ...]; tables: {"headers": [...], "rows": [[...]]}).
// ---------------------------------------------------------------------------
struct JVal {
	enum class Type { STRING, ARRAY, OBJECT, OTHER };
	Type type = Type::OTHER;
	string str;
	vector<JVal> arr;
	vector<std::pair<string, JVal>> obj;

	const JVal *Find(const string &key) const {
		for (auto &kv : obj) {
			if (kv.first == key) {
				return &kv.second;
			}
		}
		return nullptr;
	}
};

struct JsonParser {
	const string &s;
	size_t pos = 0;

	explicit JsonParser(const string &input) : s(input) {
	}

	void SkipWs() {
		while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r')) {
			pos++;
		}
	}

	static void AppendCodepoint(string &out, uint32_t cp) {
		char buf[4];
		int sz = 0;
		if (Utf8Proc::CodepointToUtf8((int)cp, sz, buf)) {
			out.append(buf, sz);
		}
	}

	string ParseString() {
		string out;
		D_ASSERT(s[pos] == '"');
		pos++;
		while (pos < s.size() && s[pos] != '"') {
			if (s[pos] == '\\' && pos + 1 < s.size()) {
				pos++;
				switch (s[pos]) {
				case 'n':
					out += '\n';
					break;
				case 't':
					out += '\t';
					break;
				case 'r':
					out += '\r';
					break;
				case 'b':
					out += '\b';
					break;
				case 'f':
					out += '\f';
					break;
				case 'u': {
					if (pos + 4 < s.size()) {
						uint32_t cp = (uint32_t)strtoul(s.substr(pos + 1, 4).c_str(), nullptr, 16);
						pos += 4;
						// surrogate pair
						if (cp >= 0xD800 && cp <= 0xDBFF && pos + 6 < s.size() && s[pos + 1] == '\\' &&
						    s[pos + 2] == 'u') {
							uint32_t lo = (uint32_t)strtoul(s.substr(pos + 3, 4).c_str(), nullptr, 16);
							if (lo >= 0xDC00 && lo <= 0xDFFF) {
								cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
								pos += 6;
							}
						}
						AppendCodepoint(out, cp);
					}
					break;
				}
				default:
					out += s[pos];
					break;
				}
				pos++;
			} else {
				out += s[pos++];
			}
		}
		if (pos < s.size()) {
			pos++; // closing quote
		}
		return out;
	}

	JVal Parse() {
		JVal v;
		SkipWs();
		if (pos >= s.size()) {
			return v;
		}
		char c = s[pos];
		if (c == '"') {
			v.type = JVal::Type::STRING;
			v.str = ParseString();
		} else if (c == '[') {
			v.type = JVal::Type::ARRAY;
			pos++;
			SkipWs();
			if (pos < s.size() && s[pos] == ']') {
				pos++;
				return v;
			}
			while (pos < s.size()) {
				v.arr.push_back(Parse());
				SkipWs();
				if (pos < s.size() && s[pos] == ',') {
					pos++;
					continue;
				}
				break;
			}
			if (pos < s.size() && s[pos] == ']') {
				pos++;
			}
		} else if (c == '{') {
			v.type = JVal::Type::OBJECT;
			pos++;
			SkipWs();
			if (pos < s.size() && s[pos] == '}') {
				pos++;
				return v;
			}
			while (pos < s.size()) {
				SkipWs();
				if (pos >= s.size() || s[pos] != '"') {
					break;
				}
				string key = ParseString();
				SkipWs();
				if (pos < s.size() && s[pos] == ':') {
					pos++;
				}
				v.obj.emplace_back(key, Parse());
				SkipWs();
				if (pos < s.size() && s[pos] == ',') {
					pos++;
					continue;
				}
				break;
			}
			if (pos < s.size() && s[pos] == '}') {
				pos++;
			}
		} else {
			// number / true / false / null — capture raw token as string
			v.type = JVal::Type::OTHER;
			size_t start = pos;
			while (pos < s.size() && s[pos] != ',' && s[pos] != ']' && s[pos] != '}' && s[pos] != ' ' &&
			       s[pos] != '\n' && s[pos] != '\t' && s[pos] != '\r') {
				pos++;
			}
			v.str = s.substr(start, pos - start);
		}
		return v;
	}
};

static string JValToText(const JVal &v) {
	switch (v.type) {
	case JVal::Type::STRING:
	case JVal::Type::OTHER:
		return v.str;
	default:
		return "";
	}
}

// ---------------------------------------------------------------------------
// Terminal width detection
// ---------------------------------------------------------------------------
static idx_t DetectTerminalWidth() {
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
	int fd = open("/dev/tty", O_RDONLY | O_CLOEXEC);
	if (fd >= 0) {
		struct winsize ws;
		int rc = ioctl(fd, TIOCGWINSZ, &ws);
		close(fd);
		if (rc == 0 && ws.ws_col > 0) {
			return (idx_t)ws.ws_col;
		}
	}
#endif
	const char *cols = getenv("COLUMNS");
	if (cols) {
		long v = atol(cols);
		if (v > 0) {
			return (idx_t)v;
		}
	}
	return DEFAULT_WIDTH;
}

// ---------------------------------------------------------------------------
// Field access helpers (Value-based, matching extraction.cpp style)
// ---------------------------------------------------------------------------
static string GetStringField(const Value &element, idx_t field_idx) {
	auto &children = StructValue::GetChildren(element);
	if (field_idx >= children.size() || children[field_idx].IsNull()) {
		return "";
	}
	return children[field_idx].GetValue<string>();
}

static int GetIntField(const Value &element, idx_t field_idx) {
	auto &children = StructValue::GetChildren(element);
	if (field_idx >= children.size() || children[field_idx].IsNull()) {
		return 0;
	}
	return children[field_idx].GetValue<int32_t>();
}

static string GetAttribute(const Value &element, const string &key) {
	auto &children = StructValue::GetChildren(element);
	auto &attrs = children[BlockTypes::ATTRIBUTES_IDX];
	if (attrs.IsNull()) {
		return "";
	}
	auto &map_entries = MapValue::GetChildren(attrs);
	for (auto &entry : map_entries) {
		if (entry.IsNull()) {
			continue;
		}
		auto &kv = StructValue::GetChildren(entry);
		if (kv.size() >= 2 && !kv[0].IsNull() && kv[0].GetValue<string>() == key) {
			if (!kv[1].IsNull()) {
				return kv[1].GetValue<string>();
			}
		}
	}
	return "";
}

// ---------------------------------------------------------------------------
// Structured inline rendering. Rich text is kind='inline' child elements
// (bold, italic, code, link, ...) with LITERAL content and encoding='text' --
// never markdown syntax inside content. We style by element_type using targeted
// SGR open/close codes so a block's base style (e.g. heading color) survives
// across the run. This extension is format-agnostic: it renders the structured
// duck_block model, not any one input format's syntax.
// ---------------------------------------------------------------------------
static void InlineStyleCodes(const string &element_type, const ThemePalette &theme, string &open, string &close) {
	if (element_type == BlockTypes::INLINE_BOLD) {
		open = Sgr("1");
		close = Sgr("22");
	} else if (element_type == BlockTypes::INLINE_ITALIC) {
		open = Sgr("3");
		close = Sgr("23");
	} else if (element_type == BlockTypes::INLINE_UNDERLINE) {
		open = Sgr("4");
		close = Sgr("24");
	} else if (element_type == BlockTypes::INLINE_STRIKETHROUGH) {
		open = Sgr("9");
		close = Sgr("29");
	} else if (element_type == BlockTypes::INLINE_CODE || element_type == BlockTypes::INLINE_MATH) {
		open = Sgr(theme.code_inline);
		close = Sgr("39");
	} else {
		open.clear();
		close.clear();
	}
}

// Consume the run of kind='inline' elements starting at `i` (advancing it past
// them) and return the styled string. Nesting is reconstructed from `level`: a
// container element has empty content and styles its deeper-level children.
static string RenderInlineRun(const vector<Value> &list, idx_t &i, const ThemePalette &theme) {
	string out;
	vector<std::pair<int, string>> stack; // (level, deferred close string)
	while (i < list.size()) {
		auto &el = list[i];
		if (el.IsNull()) {
			i++;
			continue;
		}
		if (GetStringField(el, BlockTypes::KIND_IDX) != BlockTypes::KIND_INLINE) {
			break;
		}
		int level = GetIntField(el, BlockTypes::LEVEL_IDX);
		while (!stack.empty() && stack.back().first >= level) {
			out += stack.back().second;
			stack.pop_back();
		}
		auto etype = GetStringField(el, BlockTypes::ELEMENT_TYPE_IDX);
		auto content = GetStringField(el, BlockTypes::CONTENT_IDX);

		if (etype == BlockTypes::INLINE_LINK) {
			string href = GetAttribute(el, "href");
			string suffix = href.empty() ? "" : Sgr(theme.dim) + " (" + href + ")" + RESET;
			if (!content.empty()) {
				out += Sgr(theme.link) + content + RESET + suffix;
			} else {
				out += Sgr(theme.link);
				stack.push_back({level, string(RESET) + suffix});
			}
			i++;
			continue;
		}
		if (etype == BlockTypes::INLINE_IMAGE) {
			string alt = content.empty() ? GetAttribute(el, "alt") : content;
			out += Sgr(theme.dim) + "[image: " + alt + "]" + RESET;
			i++;
			continue;
		}

		string open, close;
		InlineStyleCodes(etype, theme, open, close);
		if (!content.empty()) {
			out += open + content + close; // leaf
		} else {
			out += open; // container: defer close until level drops
			stack.push_back({level, close});
		}
		i++;
	}
	while (!stack.empty()) {
		out += stack.back().second;
		stack.pop_back();
	}
	return out;
}

// ---------------------------------------------------------------------------
// Block renderers: each appends fully assembled lines
// ---------------------------------------------------------------------------
static void RenderHeading(const string &content, const string &level_attr, size_t width, const ThemePalette &theme,
                          vector<string> &lines) {
	string style = theme.h3;
	if (level_attr == "1") {
		style = theme.h1;
	} else if (level_attr == "2" || level_attr.empty()) {
		style = theme.h2;
	}
	// Whole heading (prefix + text) shares one style; wrap with the bar on
	// every line.
	string styled = Sgr(style) + content;
	WrapPrefix prefix;
	prefix.first = Sgr(style) + "▍ ";
	prefix.cont = prefix.first;
	prefix.first_width = prefix.cont_width = 2;
	for (auto &l : WrapStyled(styled, width, prefix)) {
		lines.push_back(l);
	}
}

static void RenderParagraph(const string &content, size_t width, vector<string> &lines) {
	auto prefix = MakePrefix("", "", "");
	for (auto &l : WrapStyled(content, width, prefix)) {
		lines.push_back(l);
	}
}

static void RenderCode(const string &content, const string &lang, const ThemePalette &theme, vector<string> &lines) {
	string gutter = Sgr(theme.dim) + "┃ " + RESET;
	lines.push_back(gutter + Sgr(theme.code_lang) + lang + RESET);
	string body = content;
	while (!body.empty() && body.back() == '\n') {
		body.pop_back();
	}
	size_t start = 0;
	while (true) {
		size_t nl = body.find('\n', start);
		string code_line = body.substr(start, nl == string::npos ? string::npos : nl - start);
		// Code is not word-wrapped (would corrupt meaning); long lines overflow.
		lines.push_back(gutter + Sgr(theme.code_text) + code_line + RESET);
		if (nl == string::npos) {
			break;
		}
		start = nl + 1;
	}
}

static void RenderListItems(const JVal &items, bool ordered, int depth, size_t width, const ThemePalette &theme,
                            vector<string> &lines) {
	int number = 1;
	for (auto &item : items.arr) {
		if (item.type == JVal::Type::ARRAY) {
			// nested list
			RenderListItems(item, ordered, depth + 1, width, theme, lines);
			continue;
		}
		string indent((size_t)depth * 2, ' ');
		string marker;
		if (ordered) {
			string num = std::to_string(number++);
			marker = (num.size() < 2 ? string(2 - num.size(), ' ') : "") + num + ". ";
		} else {
			marker = "  • ";
		}
		// display width of the bullet marker: "  • " = 4 columns
		WrapPrefix prefix;
		prefix.first = indent + Sgr(theme.bullet) + marker + RESET;
		prefix.first_width = indent.size() + (ordered ? marker.size() : 4);
		prefix.cont = indent + string(ordered ? marker.size() : 4, ' ');
		prefix.cont_width = prefix.first_width;

		string text;
		if (item.type == JVal::Type::OBJECT) {
			auto *t = item.Find("text");
			text = t ? JValToText(*t) : "";
		} else {
			text = JValToText(item);
		}
		for (auto &l : WrapStyled(text, width, prefix)) {
			lines.push_back(l);
		}
		if (item.type == JVal::Type::OBJECT) {
			auto *sub = item.Find("items");
			if (sub && sub->type == JVal::Type::ARRAY) {
				auto *ord = item.Find("ordered");
				RenderListItems(*sub, ord && ord->str == "true", depth + 1, width, theme, lines);
			}
		}
	}
}

static void RenderBlockquote(const string &content, size_t width, const ThemePalette &theme, vector<string> &lines) {
	WrapPrefix prefix;
	prefix.first = Sgr(theme.quote) + "▌ " + RESET;
	prefix.cont = prefix.first;
	prefix.first_width = prefix.cont_width = 2;
	size_t start = 0;
	while (start <= content.size()) {
		size_t nl = content.find('\n', start);
		string logical = content.substr(start, nl == string::npos ? string::npos : nl - start);
		string styled = Sgr(theme.quote) + logical + RESET;
		for (auto &l : WrapStyled(styled, width, prefix)) {
			lines.push_back(l);
		}
		if (nl == string::npos) {
			break;
		}
		start = nl + 1;
	}
}

static void RenderTable(const JVal &table, size_t width, const ThemePalette &theme, vector<string> &lines) {
	auto *headers_v = table.Find("headers");
	auto *rows_v = table.Find("rows");
	if (!headers_v || headers_v->type != JVal::Type::ARRAY || headers_v->arr.empty()) {
		return;
	}
	idx_t ncols = headers_v->arr.size();

	// Styled cell text
	vector<string> header_cells;
	vector<size_t> natural(ncols, 0);
	for (idx_t c = 0; c < ncols; c++) {
		header_cells.push_back(Sgr(theme.bold_hdr) + JValToText(headers_v->arr[c]) + RESET);
		natural[c] = DisplayWidth(header_cells[c]);
	}
	vector<vector<string>> row_cells;
	if (rows_v && rows_v->type == JVal::Type::ARRAY) {
		for (auto &row : rows_v->arr) {
			vector<string> cells(ncols);
			for (idx_t c = 0; c < ncols && c < row.arr.size(); c++) {
				cells[c] = JValToText(row.arr[c]);
				natural[c] = MaxValue<size_t>(natural[c], DisplayWidth(cells[c]));
			}
			row_cells.push_back(std::move(cells));
		}
	}

	// Fit columns into the width budget
	size_t sep_total = 3 * (ncols - 1);
	size_t budget = width > sep_total + ncols ? width - sep_total : ncols;
	vector<size_t> col(ncols);
	size_t total_natural = 0;
	for (idx_t c = 0; c < ncols; c++) {
		total_natural += natural[c];
	}
	if (total_natural <= budget) {
		col = natural;
	} else {
		// start from a fair share, give unused share back to hungry columns
		size_t fair = MaxValue<size_t>(MIN_TABLE_COL, budget / ncols);
		size_t spare = 0;
		size_t hungry = 0;
		for (idx_t c = 0; c < ncols; c++) {
			if (natural[c] <= fair) {
				col[c] = natural[c];
				spare += fair - natural[c];
			} else {
				col[c] = fair;
				hungry++;
			}
		}
		while (spare > 0 && hungry > 0) {
			size_t gave = 0;
			for (idx_t c = 0; c < ncols && spare > 0; c++) {
				if (col[c] < natural[c]) {
					col[c]++;
					spare--;
					gave++;
				}
			}
			if (gave == 0) {
				break;
			}
			hungry = 0;
			for (idx_t c = 0; c < ncols; c++) {
				if (col[c] < natural[c]) {
					hungry++;
				}
			}
		}
	}

	string sep = Sgr(theme.dim) + " │ " + RESET;

	// Emit one logical row as (possibly) multiple physical lines
	auto emit_row = [&](const vector<string> &cells) {
		// wrap each cell into its column width
		vector<vector<string>> wrapped(ncols);
		size_t height = 1;
		for (idx_t c = 0; c < ncols; c++) {
			auto prefix = MakePrefix("", "", "");
			wrapped[c] = WrapStyled(c < cells.size() ? cells[c] : "", col[c], prefix);
			height = MaxValue<size_t>(height, wrapped[c].size());
		}
		for (size_t h = 0; h < height; h++) {
			string line;
			for (idx_t c = 0; c < ncols; c++) {
				if (c > 0) {
					line += sep;
				}
				string cell = h < wrapped[c].size() ? wrapped[c][h] : "";
				line += PadTo(cell, col[c]);
			}
			// trim trailing spaces of the raw right edge
			while (!line.empty() && line.back() == ' ') {
				line.pop_back();
			}
			lines.push_back(line);
		}
	};

	emit_row(header_cells);
	{
		string rule = Sgr(theme.dim);
		for (idx_t c = 0; c < ncols; c++) {
			if (c > 0) {
				rule += "─┼─";
			}
			for (size_t k = 0; k < col[c]; k++) {
				rule += "─";
			}
		}
		rule += RESET;
		lines.push_back(rule);
	}
	for (auto &cells : row_cells) {
		emit_row(cells);
	}
}

static void RenderHr(size_t width, const ThemePalette &theme, vector<string> &lines) {
	size_t n = MinValue<size_t>(width, 64);
	string rule = Sgr(theme.dim);
	for (size_t i = 0; i < n; i++) {
		rule += "─";
	}
	rule += RESET;
	lines.push_back(rule);
}

static void RenderImage(const Value &block, const string &content, const ThemePalette &theme, vector<string> &lines) {
	string alt = GetAttribute(block, "alt");
	string src = GetAttribute(block, "src");
	if (alt.empty()) {
		alt = content;
	}
	lines.push_back(Sgr(theme.dim) + "[image: " + alt + "]" + (src.empty() ? "" : " (" + src + ")") + RESET);
}

// Render one document; returns empty string for empty/inline-only input
static string RenderDocument(const Value &blocks_val, size_t width, const ThemePalette &theme) {
	if (width < MIN_WIDTH) {
		width = MIN_WIDTH;
	}
	auto &blocks_list = ListValue::GetChildren(blocks_val);
	string out;
	bool first = true;
	idx_t bi = 0;
	while (bi < blocks_list.size()) {
		auto &block = blocks_list[bi];
		if (block.IsNull()) {
			bi++;
			continue;
		}
		auto kind = GetStringField(block, BlockTypes::KIND_IDX);
		if (kind != BlockTypes::KIND_BLOCK) {
			// A stray inline with no parent block: skip (it is not a document).
			bi++;
			continue;
		}
		auto element_type = GetStringField(block, BlockTypes::ELEMENT_TYPE_IDX);
		auto content = GetStringField(block, BlockTypes::CONTENT_IDX);

		// Gather this block's structured inline children (rich text). Per spec,
		// `content` is populated iff the container has a single text child, so a
		// non-empty content is the literal simple case; otherwise the styled run
		// of inline children is the text.
		idx_t j = bi + 1;
		string inline_text = RenderInlineRun(blocks_list, j, theme);
		string text = content.empty() ? inline_text : content;

		vector<string> lines;
		if (element_type == BlockTypes::TYPE_HEADING) {
			RenderHeading(text, GetAttribute(block, "heading_level"), width, theme, lines);
		} else if (element_type == BlockTypes::TYPE_PARAGRAPH) {
			RenderParagraph(text, width, lines);
		} else if (element_type == BlockTypes::TYPE_CODE) {
			RenderCode(content, GetAttribute(block, "language"), theme, lines);
		} else if (element_type == BlockTypes::TYPE_LIST) {
			JsonParser parser(content);
			auto items = parser.Parse();
			if (items.type == JVal::Type::ARRAY) {
				RenderListItems(items, GetAttribute(block, "ordered") == "true", 0, width, theme, lines);
			}
		} else if (element_type == BlockTypes::TYPE_TABLE) {
			JsonParser parser(content);
			auto table = parser.Parse();
			if (table.type == JVal::Type::OBJECT) {
				RenderTable(table, width, theme, lines);
			}
		} else if (element_type == BlockTypes::TYPE_BLOCKQUOTE) {
			RenderBlockquote(text, width, theme, lines);
		} else if (element_type == BlockTypes::TYPE_HR) {
			RenderHr(width, theme, lines);
		} else if (element_type == BlockTypes::TYPE_METADATA) {
			// suppressed
		} else if (element_type == BlockTypes::TYPE_IMAGE) {
			RenderImage(block, content, theme, lines);
		} else if (!text.empty()) {
			RenderParagraph(text, width, lines);
		}

		bi = j; // advance past the consumed inline children

		if (lines.empty()) {
			continue;
		}
		if (!first) {
			out += "\n\n";
		}
		first = false;
		for (idx_t i = 0; i < lines.size(); i++) {
			if (i > 0) {
				out += "\n";
			}
			out += lines[i];
		}
	}
	return out;
}

// ---------------------------------------------------------------------------
// Scalar function implementations
// ---------------------------------------------------------------------------
static void RenderAnsiFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &blocks_vec = args.data[0];
	auto count = args.size();
	bool has_arg1 = args.ColumnCount() > 1;
	bool has_arg2 = args.ColumnCount() > 2;

	for (idx_t i = 0; i < count; i++) {
		auto blocks_val = blocks_vec.GetValue(i);
		if (blocks_val.IsNull()) {
			result.SetValue(i, Value());
			continue;
		}
		int64_t width = 0;
		string theme_name;

		if (has_arg1) {
			auto arg1_val = args.data[1].GetValue(i);
			if (!arg1_val.IsNull()) {
				if (arg1_val.type().id() == LogicalTypeId::VARCHAR) {
					theme_name = StringValue::Get(arg1_val);
				} else if (arg1_val.type().IsNumeric()) {
					width = arg1_val.GetValue<int64_t>();
				}
			}
		}
		if (has_arg2) {
			auto arg2_val = args.data[2].GetValue(i);
			if (!arg2_val.IsNull()) {
				if (arg2_val.type().id() == LogicalTypeId::VARCHAR) {
					theme_name = StringValue::Get(arg2_val);
				} else if (arg2_val.type().IsNumeric()) {
					width = arg2_val.GetValue<int64_t>();
				}
			}
		}
		idx_t effective = width > 0 ? (idx_t)width : DetectTerminalWidth();
		const auto &palette = ResolveTheme(theme_name);
		result.SetValue(i, Value(RenderDocument(blocks_val, effective, palette)));
	}
}

static void TerminalWidthFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto width = (int32_t)DetectTerminalWidth();
	result.SetVectorType(VectorType::CONSTANT_VECTOR);
	result.SetValue(0, Value::INTEGER(width));
}

} // namespace

void RenderAnsiFunctions::Register(ExtensionLoader &loader) {
	auto duck_block_list_type = BlockTypes::DuckBlockListType();

	// db_blocks_render_ansi(blocks) -> VARCHAR (auto-detected width and theme)
	auto render_auto =
	    ScalarFunction("db_blocks_render_ansi", {duck_block_list_type}, LogicalType::VARCHAR, RenderAnsiFun);
	loader.RegisterFunction(render_auto);

	// db_blocks_render_ansi(blocks, width) -> VARCHAR
	auto render_width = ScalarFunction("db_blocks_render_ansi", {duck_block_list_type, LogicalType::INTEGER},
	                                   LogicalType::VARCHAR, RenderAnsiFun);
	loader.RegisterFunction(render_width);

	// db_blocks_render_ansi(blocks, theme) -> VARCHAR
	auto render_theme = ScalarFunction("db_blocks_render_ansi", {duck_block_list_type, LogicalType::VARCHAR},
	                                   LogicalType::VARCHAR, RenderAnsiFun);
	loader.RegisterFunction(render_theme);

	// db_blocks_render_ansi(blocks, width, theme) -> VARCHAR
	auto render_width_theme =
	    ScalarFunction("db_blocks_render_ansi", {duck_block_list_type, LogicalType::INTEGER, LogicalType::VARCHAR},
	                   LogicalType::VARCHAR, RenderAnsiFun);
	loader.RegisterFunction(render_width_theme);

	// db_blocks_render_ansi(blocks, theme, width) -> VARCHAR
	auto render_theme_width =
	    ScalarFunction("db_blocks_render_ansi", {duck_block_list_type, LogicalType::VARCHAR, LogicalType::INTEGER},
	                   LogicalType::VARCHAR, RenderAnsiFun);
	loader.RegisterFunction(render_theme_width);

	// db_terminal_width() -> INTEGER
	auto term_width = ScalarFunction("db_terminal_width", {}, LogicalType::INTEGER, TerminalWidthFun);
	term_width.stability = FunctionStability::VOLATILE;
	loader.RegisterFunction(term_width);
}

} // namespace duckdb
