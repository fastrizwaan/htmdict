#!/usr/bin/env python3
"""
DSL to Diction Converter
Converts ABBYY Lingvo DSL files to Diction .diction packages.
Spec: Diction Specification 1.0

Supported DSL tags:
  Formatting:   [b], [i], [u], [s], [sup], [sub], [c], [c color]
  Structure:    [m], [m1]–[m9], [*], [/*], ['])], [/')]
  Semantic:     [p], [t], [trn], [!trs], [ex], [com], [abr], [abbr]
  Links:        [ref], [url], << >>
  Media:        [s] (sound), [video], [img]
  Language:     [lang id=xx], [lang name=xx]
  Lists:        ['][/'] (numbered), ['][/'] (unnumbered variants)
  Misc:         [sub], [sup], [br], ['][']
"""

import re
import pathlib
import zipfile
import json
import unicodedata
import shutil
from datetime import date
from html import escape

DEFAULT_OUTPUT_NAME = "Dictionary"

DEFAULT_CSS = """\
/* ── Diction 1.0 stylesheet ── */

article.entry {
    margin-bottom: 1.5rem;
    padding: 18px;
    border: 1px solid #d0d0d0;
    border-radius: 8px;
    background: #f7f7f7;
}

headword {
    display: block;
    font-size: 1.8rem;
    font-weight: bold;
    margin-bottom: 0.2rem;
}

alias {
    display: inline-block;
    color: #666;
    font-style: italic;
    margin-right: 0.5rem;
}
alias::before { content: "= "; }

pos {
    display: inline-block;
    color: #888;
    font-style: italic;
    margin-right: 0.4rem;
}

pronunciation {
    display: block;
    color: #444;
    margin-bottom: 0.3rem;
    font-family: monospace;
}

definition {
    display: block;
    margin: 0.3rem 0 0.3rem 1rem;
}

example {
    display: block;
    margin: 0.4rem 0 0.4rem 2rem;
    color: #555;
    font-style: italic;
}

example::before { content: "▸ "; color: #aaa; }

etymology {
    display: block;
    margin-top: 0.5rem;
    color: #666;
    font-size: 0.9em;
}

etymology::before { content: "Etymology: "; font-weight: bold; }

/* Numbered definition lists */
ol.decimal { margin: 0.3rem 0 0.3rem 1.5rem; }
ol.decimal li { margin-bottom: 0.2rem; }

/* Margin levels (DSL [m0]–[m9]) */
.m0 { display: block; margin-left: 0; }
.m1 { display: block; margin-left: 1em; }
.m2 { display: block; margin-left: 2em; }
.m3 { display: block; margin-left: 3em; }
.m4 { display: block; margin-left: 4em; }
.m5 { display: block; margin-left: 5em; }
.m6 { display: block; margin-left: 6em; }
.m7 { display: block; margin-left: 7em; }
.m8 { display: block; margin-left: 8em; }
.m9 { display: block; margin-left: 9em; }

/* Optional/hidden annotation block [*]...[/*] */
.opt { display: none; }

/* Abbreviated/label text [abr]/[abbr] */
abbr.dsl { color: #888; font-style: italic; font-size: 0.9em; }

/* Comment/note [com] */
.comment { color: #999; font-size: 0.85em; }

/* Translation block [trn]/[!trs] */
.translation { display: inline; }

/* Transcription / IPA [t] */
.transcription { font-family: monospace; color: #444; }

/* Colored text [c] / [c color] */
.colored { /* inherits color from style attribute */ }

/* Cross-references entry:// */
a[href^="entry://"] { color: #1a73e8; text-decoration: none; }
a[href^="entry://"]:hover { text-decoration: underline; }

/* Sound links sound:// */
a[href^="sound://"] { text-decoration: none; font-size: 1.1em; }

/* Inline media */
img { max-width: 100%; height: auto; display: block; margin: 0.4rem 0; }
video { max-width: 100%; display: block; margin: 0.4rem 0; }
"""


# ─────────────────────────────────────────────
# Encoding helpers
# ─────────────────────────────────────────────

def read_text(file_path: pathlib.Path) -> str:
    """Read DSL file (UTF-8, UTF-16 LE/BE with BOM) and return as str."""
    raw = file_path.read_bytes()
    if raw.startswith(b'\xef\xbb\xbf'):
        return raw[3:].decode('utf-8')
    if raw.startswith(b'\xff\xfe'):
        return raw[2:].decode('utf-16-le')
    if raw.startswith(b'\xfe\xff'):
        return raw[2:].decode('utf-16-be')
    try:
        return raw.decode('utf-8')
    except UnicodeDecodeError:
        return raw.decode('latin-1')


def nfc(text: str) -> str:
    return unicodedata.normalize("NFC", text)


def sort_key(headword: str) -> str:
    return nfc(headword).casefold()


# ─────────────────────────────────────────────
# DSL inline markup → HTML
# ─────────────────────────────────────────────

# Named CSS colors and hex patterns (for [c color])
_COLOR_RE = re.compile(
    r'^(#[0-9a-fA-F]{3,8}|[a-z]+)$', re.IGNORECASE
)


def _safe_color(val: str) -> str:
    """Return val if it looks like a valid CSS color, else empty string."""
    v = val.strip()
    if _COLOR_RE.match(v):
        return v
    return ""


def clean_dsl_markup(text: str) -> str:
    """
    Convert all DSL inline markup to Diction-compatible HTML.
    Processes tags in a single sequential pass using a token loop
    to handle nesting correctly.
    """
    # ── Pre-pass: normalise line continuations and hard line breaks ──
    text = text.replace("\\\n", "")   # DSL continuation lines
    text = text.replace("[br]", "<br>")

    # ── Strip Lingvo dictionary-specific macro tags {{tag}}...{{/tag}} and {{tag}} ──
    # These are proprietary LDOCE/Lingvo template markers with no displayable
    # content of their own. Strip the tags but keep any text between them.
    text = re.sub(r'\{\{/?[^}]+\}\}', '', text)

    # ── << ... >> → entry:// link ──
    def replace_double_angle(m):
        content = nfc(m.group(1).strip())
        safe = escape(content)
        return f'<a href="entry://{escape(content, quote=True)}">{safe}</a>'
    text = re.sub(r'<<(.*?)>>', replace_double_angle, text, flags=re.DOTALL)

    # Process remaining bracket tags with a replacer loop.
    # Order matters: process inner/atomic tags before structural ones.

    # Extensions that [s] should render as <img> rather than sound://
    _IMAGE_EXTS = {'.jpg', '.jpeg', '.png', '.gif', '.webp', '.bmp', '.svg', '.tif', '.tiff'}
    # Extensions that [s] should render as <video> rather than sound://
    _VIDEO_EXTS = {'.mp4', '.webm', '.ogv', '.avi', '.mov', '.mkv'}

    def replace_s_tag(m):
        """
        [s]filename[/s] dispatches by file extension:
          image → <img src="media/..." alt="filename">
          video → <video src="media/..." controls></video>
          audio (default) → <a href="sound://media/...">🔊</a>
        """
        fname = m.group(1).strip()
        ext = pathlib.Path(fname).suffix.lower()
        safe_src = escape(fname, quote=True)
        safe_alt = escape(fname)
        if ext in _IMAGE_EXTS:
            return f'<img src="media/{safe_src}" alt="{safe_alt}">'
        if ext in _VIDEO_EXTS:
            return f'<video src="media/{safe_src}" controls></video>'
        # Default: audio / sound
        return f'<a href="sound://media/{safe_src}">🔊</a>'

    replacements = [
        # ── Cross-references ──
        (r'\[ref\](.*?)\[/ref\]',
         lambda m: f'<a href="entry://{escape(nfc(m.group(1)), quote=True)}">{escape(nfc(m.group(1)))}</a>'),

        # ── URLs ──
        (r'\[url\](.*?)\[/url\]',
         lambda m: f'<a href="{escape(m.group(1), quote=True)}">{escape(m.group(1))}</a>'),

        # ── [s] — smart dispatch: image / video / audio ──
        (r'\[s\](.*?)\[/s\]', replace_s_tag),

        # ── [video]file[/video] → <video> element ──
        (r'\[video\](.*?)\[/video\]',
         lambda m: f'<video src="media/{escape(m.group(1).strip(), quote=True)}" controls></video>'),

        # ── [img]file[/img] → <img> ──
        (r'\[img\](.*?)\[/img\]',
         lambda m: f'<img src="media/{escape(m.group(1).strip(), quote=True)}" alt="{escape(m.group(1).strip())}">'),

        # ── Language spans: [lang id=xx] or [lang name=xx] ──
        (r'\[lang\s+(?:id|name)=([^\]]+)\](.*?)\[/lang\]',
         lambda m: f'<span lang="{escape(m.group(1).strip())}">{m.group(2)}</span>'),

        # ── Superscript / subscript ──
        (r'\[sup\](.*?)\[/sup\]', r'<sup>\1</sup>'),
        (r'\[sub\](.*?)\[/sub\]', r'<sub>\1</sub>'),

        # ── Basic formatting ──
        (r'\[b\](.*?)\[/b\]',     r'<strong>\1</strong>'),
        (r'\[i\](.*?)\[/i\]',     r'<em>\1</em>'),
        (r'\[u\](.*?)\[/u\]',     r'<u>\1</u>'),

        # ── Strikethrough [s] when NOT wrapping a filename ──
        # (handled above as sound; leftover bare [s] without content)

        # ── Colored text: [c colorname] or [c] ──
        (r'\[c\s+([^\]]+)\](.*?)\[/c\]',
         lambda m: (
             f'<span class="colored" style="color:{_safe_color(m.group(1))}">{m.group(2)}</span>'
             if _safe_color(m.group(1)) else
             f'<span class="colored">{m.group(2)}</span>'
         )),
        (r'\[c\](.*?)\[/c\]',
         r'<span class="colored">\1</span>'),

        # ── Transcription / IPA [t] ──
        (r'\[t\](.*?)\[/t\]',
         r'<span class="transcription">/\1/</span>'),

        # ── Part of speech [p] ──
        (r'\[p\](.*?)\[/p\]',
         r'<pos>\1</pos>'),

        # ── Abbreviation [abr] / [abbr] ──
        (r'\[abr\](.*?)\[/abr\]',
         r'<abbr class="dsl">\1</abbr>'),
        (r'\[abbr\](.*?)\[/abbr\]',
         r'<abbr class="dsl">\1</abbr>'),

        # ── Comment [com] ──
        (r'\[com\](.*?)\[/com\]',
         r'<span class="comment">\1</span>'),

        # ── Translation block [trn] / [!trs] ──
        (r'\[trn\](.*?)\[/trn\]',
         r'<span class="translation">\1</span>'),
        (r'\[!trs\](.*?)\[/!trs\]',
         r'<span class="translation">\1</span>'),

        # ── Example [ex] ──
        (r'\[ex\](.*?)\[/ex\]',
         r'<example>\1</example>'),

        # ── Optional block [*]...[/*] → hidden ──
        (r'\[\*\](.*?)\[/\*\]',
         r'<span class="opt">\1</span>'),

        # ── Bare [*] / [/*] markers (structural, not wrapping) ──
        (r'\[\*\]', ''),
        (r'\[/\*\]', ''),

        # ── Margin markers [m] / [m1]–[m9]: strip here (handled structurally) ──
        (r'\[/?m\d*\]', ''),
    ]

    for pattern, repl in replacements:
        if callable(repl):
            text = re.sub(pattern, repl, text, flags=re.DOTALL)
        else:
            text = re.sub(pattern, repl, text, flags=re.DOTALL)

    return text.strip()


# ─────────────────────────────────────────────
# DSL file parser
# ─────────────────────────────────────────────

# Known DSL header directives
_HEADER_DIRECTIVES = {
    '#NAME', '#INDEX_LANGUAGE', '#CONTENTS_LANGUAGE',
    '#SOURCE_CODE_PAGE', '#INCLUDE', '#ICON',
}


def _extract_quoted_or_bare(line: str, directive: str) -> str:
    """Extract value from '#DIRECTIVE "value"' or '#DIRECTIVE value'."""
    rest = line[len(directive):].strip()
    m = re.match(r'^"([^"]*)"', rest)
    if m:
        return m.group(1).strip()
    return rest.strip()


def parse_dsl(file_path: pathlib.Path):
    """
    Parse a DSL file.

    Returns:
        dict: header metadata (name, index_language, contents_language, ...)
        list: entries — each is (primary_headword, [aliases], [(margin_level, cleaned_html), ...])
    """
    full_text = read_text(file_path)
    lines = full_text.splitlines()

    meta = {
        "name": None,
        "index_language": None,
        "contents_language": None,
    }

    entries = []
    current_headwords: list[str] = []
    current_body: list[tuple[int, str]] = []

    def flush_entry():
        if current_headwords and current_body:
            primary = current_headwords[0]
            aliases = current_headwords[1:]
            entries.append((primary, aliases, list(current_body)))
        current_headwords.clear()
        current_body.clear()

    for raw_line in lines:
        line = raw_line.rstrip()

        # ── Header directives ──
        if line.startswith('#NAME'):
            meta["name"] = _extract_quoted_or_bare(line, '#NAME')
            continue
        if line.startswith('#INDEX_LANGUAGE'):
            meta["index_language"] = _extract_quoted_or_bare(line, '#INDEX_LANGUAGE')
            continue
        if line.startswith('#CONTENTS_LANGUAGE'):
            meta["contents_language"] = _extract_quoted_or_bare(line, '#CONTENTS_LANGUAGE')
            continue
        if line.startswith('#'):
            continue  # skip other directives

        # ── Blank line: flush current entry ──
        if not line.strip():
            flush_entry()
            continue

        # ── Headword detection ──
        # A headword line does NOT start with whitespace
        # and is not purely a [mN] structural marker
        is_headword = (
            not line.startswith((' ', '\t'))
            and not re.match(r'^\[m\d*\]\s*$', line.strip())
        )

        if is_headword:
            # Multiple consecutive non-indented lines = multiple headwords for same entry
            # But if we already have body lines, this is a new entry
            if current_body:
                flush_entry()
            current_headwords.append(line.strip())
        else:
            # ── Body line ──
            if not current_headwords:
                continue  # orphaned body line

            stripped = line.strip()

            # Determine margin level from leading [mN] tag
            margin = 1  # default for untagged body lines
            m = re.match(r'^\[m(\d+)\]', stripped)
            if m:
                margin = int(m.group(1))   # 0–9, including [m0]
                stripped = re.sub(r'^\[m\d+\]\s*', '', stripped)
            elif re.match(r'^\[m\]', stripped):
                margin = 1
                stripped = re.sub(r'^\[m\]\s*', '', stripped)

            if not stripped:
                continue

            cleaned = clean_dsl_markup(stripped)
            if cleaned:
                current_body.append((margin, cleaned))

    # End of file
    flush_entry()

    return meta, entries


# ─────────────────────────────────────────────
# DSL language code → BCP 47
# ─────────────────────────────────────────────

# Partial map of Lingvo language names to BCP 47 tags.
# Extend as needed.
_LINGVO_LANG_MAP: dict[str, str] = {
    "english":    "en",
    "russian":    "ru",
    "german":     "de",
    "french":     "fr",
    "spanish":    "es",
    "italian":    "it",
    "portuguese": "pt",
    "dutch":      "nl",
    "polish":     "pl",
    "turkish":    "tr",
    "greek":      "el",
    "arabic":     "ar",
    "persian":    "fa",
    "hebrew":     "he",
    "hindi":      "hi",
    "japanese":   "ja",
    "chinese":    "zh",
    "korean":     "ko",
    "ukrainian":  "uk",
    "czech":      "cs",
    "slovak":     "sk",
    "romanian":   "ro",
    "hungarian":  "hu",
    "swedish":    "sv",
    "norwegian":  "no",
    "danish":     "da",
    "finnish":    "fi",
    "latvian":    "lv",
    "lithuanian": "lt",
    "estonian":   "et",
    "georgian":   "ka",
    "armenian":   "hy",
    "azerbaijani":"az",
    "kazakh":     "kk",
    "uzbek":      "uz",
    "albanian":   "sq",
    "serbian":    "sr",
    "croatian":   "hr",
    "bosnian":    "bs",
    "bulgarian":  "bg",
    "macedonian": "mk",
    "slovenian":  "sl",
    "latin":      "la",
    "esperanto":  "eo",
    "vietnamese": "vi",
    "thai":       "th",
    "indonesian": "id",
    "malay":      "ms",
    "swahili":    "sw",
}


def lingvo_lang_to_bcp47(lang_name: str | None) -> str:
    """Convert a Lingvo language name to a BCP 47 tag, falling back to 'und'."""
    if not lang_name:
        return "und"
    key = lang_name.strip().lower()
    return _LINGVO_LANG_MAP.get(key, key)


# ─────────────────────────────────────────────
# HTML fragment builder
# ─────────────────────────────────────────────

def build_header_comment(name: str | None, index_lang: str, content_lang: str) -> str | None:
    """
    Build a single header comment combining name and language pair.

    Rules:
    - Language suffix is "(xx-yy)" from index_lang and content_lang.
    - If name already ends with a parenthesised tag like "(en-ru)", strip it
      before appending the authoritative one from the DSL headers.
    - If both langs are "und" and name is absent, return None.
    - Format:  <!-- Name: "My Dict (en-ru)" -->
    """
    lang_suffix = None
    if index_lang != "und" or content_lang != "und":
        lang_suffix = f"({index_lang}-{content_lang})"

    base_name = name.strip() if name else None

    # Strip any existing trailing "(xx-yy)" / "(xx)" parenthesised tag from name
    if base_name:
        base_name = re.sub(r'\s*\([a-zA-Z0-9]+-?[a-zA-Z0-9]*\)\s*$', '', base_name).strip()

    if base_name and lang_suffix:
        label = f'"{base_name} {lang_suffix}"'
    elif base_name:
        label = f'"{base_name}"'
    elif lang_suffix:
        label = lang_suffix
    else:
        return None

    return f'<!-- Name: {label} -->'


def build_html(entries, name: str | None = None,
               index_lang: str = "und", content_lang: str = "und") -> str:
    """
    Build the Diction HTML fragment stream from parsed entries.

    Structural rules:
    - Body lines at margin level 1 become <definition> elements.
    - Body lines at higher margin levels (2+) become <definition class="mN">.
    - <example> tags inside body are preserved as-is from markup conversion.
    - Entries are emitted in the order supplied (sort before calling).
    """
    out: list[str] = []
    comment = build_header_comment(name, index_lang, content_lang)
    if comment:
        out.append(comment)
        out.append('')

    for primary, aliases, body in entries:
        out.append('<article class="entry">')
        out.append(f'  <headword>{escape(primary)}</headword>')
        for alias in aliases:
            out.append(f'  <alias>{escape(alias)}</alias>')

        for level, html_content in body:
            # Wrap each body line in a <definition> with margin class
            cls = f'm{level}'  # m0–m9
            out.append(f'  <definition class="{cls}">{html_content}</definition>')

        out.append('</article>')
        out.append('')  # blank line between entries for readability

    return "\n".join(out)


# ─────────────────────────────────────────────
# Packaging
# ─────────────────────────────────────────────

# File extensions that should be stored uncompressed in ZIP
_STORED_EXTENSIONS = {
    ".mp3", ".mp4", ".ogg", ".webm",
    ".jpg", ".jpeg", ".png", ".gif", ".webp", ".svg",
    ".woff", ".woff2", ".ttf", ".otf",
}


def package_diction(output_dir: pathlib.Path) -> pathlib.Path:
    """Zip the output directory into a .diction archive."""
    diction_path = output_dir.with_suffix(".diction")
    with zipfile.ZipFile(diction_path, "w", allowZip64=True) as zf:
        for file in sorted(output_dir.rglob("*")):
            if not file.is_file():
                continue
            arcname = file.relative_to(output_dir.parent)
            method = (
                zipfile.ZIP_STORED
                if file.suffix.lower() in _STORED_EXTENSIONS
                else zipfile.ZIP_DEFLATED
            )
            zf.write(file, arcname, compress_type=method)
    return diction_path


# ─────────────────────────────────────────────
# Main converter
# ─────────────────────────────────────────────

def _sanitize_id(name: str) -> str:
    """Make a valid meta.json id: lowercase, replace non-alnum with _."""
    return re.sub(r'[^a-z0-9_-]', '_', name.lower())


def _sanitize_filename(name: str) -> str:
    """Remove filesystem-unsafe characters from a name."""
    return re.sub(r'[<>:"/\\|?*\x00-\x1f]', '_', name).strip()


def extract_media_assets(dsl_path: pathlib.Path, media_dir: pathlib.Path) -> None:
    """
    Locate and unpack companion media assets into media_dir.

    Checks in order (all relative to the DSL file's directory):
      1. <stem>.files.zip        — most common Lingvo companion archive
      2. <stem>.files/           — pre-extracted folder variant
      3. media/                  — generic media folder next to the DSL
      4. <stem>/                 — folder named after the dictionary stem

    All files from ZIPs are extracted flat into media_dir (subdirectories
    inside the ZIP are preserved as-is, so nested paths work too).
    Files from folders are copied recursively into media_dir.

    Existing files in media_dir are never overwritten (first source wins),
    so you can stack sources without surprises.
    """
    dsl_dir = dsl_path.parent
    stem = dsl_path.stem
    total = 0

    def _safe_rel_path(path: str | pathlib.PurePath) -> pathlib.Path | None:
        """Return a safe relative path, or None for absolute/traversal paths."""
        pure = pathlib.PurePosixPath(str(path).replace("\\", "/"))
        if pure.is_absolute():
            return None
        parts = [p for p in pure.parts if p not in ("", ".")]
        if not parts or any(p == ".." for p in parts):
            return None
        return pathlib.Path(*parts)

    def _copy_file(src: pathlib.Path, dest: pathlib.Path) -> bool:
        """Copy src to dest; skip if dest already exists. Returns True if copied."""
        if dest.exists():
            return False
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dest)
        return True

    # 1. <stem>.files.zip
    files_zip = dsl_dir / f"{stem}.files.zip"
    if files_zip.exists():
        print(f"  Extracting media from {files_zip.name} …")
        with zipfile.ZipFile(files_zip) as zf:
            for member in zf.infolist():
                if member.is_dir():
                    continue
                # Flatten: strip any leading directory component that is just
                # the archive stem (e.g. "MyDict.files/sound.mp3" → "sound.mp3")
                member_path = pathlib.PurePosixPath(member.filename)
                parts = member_path.parts
                if len(parts) > 1 and parts[0].lower() in (
                    stem.lower(), f"{stem.lower()}.files"
                ):
                    rel = _safe_rel_path(pathlib.PurePosixPath(*parts[1:]))
                else:
                    rel = _safe_rel_path(member.filename)
                if rel is None:
                    print(f"  Skipping unsafe media path: {member.filename}")
                    continue
                dest = media_dir / rel
                if not dest.exists():
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    with zf.open(member) as src_f, open(dest, "wb") as dst_f:
                        shutil.copyfileobj(src_f, dst_f)
                    total += 1
        print(f"  → {total} media file(s) extracted from ZIP")
        return  # ZIP is the canonical source; stop here

    # 2. <stem>.files/  folder
    files_dir = dsl_dir / f"{stem}.files"
    if files_dir.is_dir():
        print(f"  Copying media from {files_dir.name}/ …")
        for src in files_dir.rglob("*"):
            if src.is_file():
                rel = src.relative_to(files_dir)
                if _copy_file(src, media_dir / rel):
                    total += 1
        print(f"  → {total} media file(s) copied from .files/ folder")
        return

    # 3. media/ folder next to the DSL
    adjacent_media = dsl_dir / "media"
    if adjacent_media.is_dir():
        print(f"  Copying media from adjacent media/ …")
        for src in adjacent_media.rglob("*"):
            if src.is_file():
                rel = src.relative_to(adjacent_media)
                if _copy_file(src, media_dir / rel):
                    total += 1
        if total:
            print(f"  → {total} media file(s) copied from media/")
            return

    # 4. <stem>/ folder (some Lingvo exports drop files here)
    stem_dir = dsl_dir / stem
    if stem_dir.is_dir():
        print(f"  Copying media from {stem_dir.name}/ …")
        for src in stem_dir.rglob("*"):
            if src.is_file():
                rel = src.relative_to(stem_dir)
                if _copy_file(src, media_dir / rel):
                    total += 1
        if total:
            print(f"  → {total} media file(s) copied from stem folder")


def convert_dsl(dsl_file: str, output_name: str | None = None) -> pathlib.Path:
    """
    Convert a DSL file to a .diction package.

    Args:
        dsl_file:    Path to the input .dsl file.
        output_name: Optional override for the output package base name.

    Returns:
        Path to the created .diction file.
    """
    dsl_path = pathlib.Path(dsl_file)
    if not dsl_path.exists():
        raise FileNotFoundError(f"File not found: {dsl_file}")

    dsl_meta, entries = parse_dsl(dsl_path)

    # ── Determine output name ──
    if not output_name:
        if dsl_meta["name"]:
            output_name = _sanitize_filename(dsl_meta["name"])
        else:
            output_name = dsl_path.stem
    if not output_name:
        output_name = DEFAULT_OUTPUT_NAME

    # ── Sort entries alphabetically (spec §34) ──
    entries.sort(key=lambda e: sort_key(e[0]))

    # ── Resolve BCP 47 language tags ──
    index_lang = lingvo_lang_to_bcp47(dsl_meta["index_language"])
    content_lang = lingvo_lang_to_bcp47(dsl_meta["contents_language"])

    # ── Build meta.json ──
    html_filename = f"{output_name}.html"
    meta = {
        "format": 1,
        "id": _sanitize_id(output_name),
        "name": dsl_meta["name"] or output_name,
        "short_name": output_name[:20],
        "index_languages": [index_lang],
        "content_languages": [content_lang],
        "version": "1.0",
        "created": date.today().isoformat(),
        "stylesheet": "style.css",
        "html": html_filename,
    }

    # ── Build HTML fragment stream ──
    html_content = build_html(entries, dsl_meta["name"], index_lang, content_lang)

    # ── Write output directory ──
    root = pathlib.Path(output_name)
    if root.exists():
        shutil.rmtree(root)
    root.mkdir()
    (root / "media").mkdir()
    (root / "fonts").mkdir()

    # ── Extract companion media assets into media/ ──
    extract_media_assets(dsl_path, root / "media")

    (root / html_filename).write_text(html_content, encoding="utf-8", newline="\n")
    (root / "style.css").write_text(DEFAULT_CSS, encoding="utf-8", newline="\n")
    (root / "meta.json").write_text(
        json.dumps(meta, indent=2, ensure_ascii=False),
        encoding="utf-8",
        newline="\n",
    )

    # ── Package ──
    diction_path = package_diction(root)
    print(f"✓  {len(entries)} entries → {diction_path}")
    return diction_path


# ─────────────────────────────────────────────
# CLI entry point
# ─────────────────────────────────────────────

if __name__ == "__main__":
    import sys
    import argparse

    parser = argparse.ArgumentParser(
        description="Convert an ABBYY Lingvo DSL dictionary to a Diction .diction package."
    )
    parser.add_argument("dsl_file", help="Path to input .dsl file")
    parser.add_argument(
        "output_name", nargs="?", default=None,
        help="Output base name (default: derived from #NAME header or filename)"
    )
    args = parser.parse_args()

    try:
        convert_dsl(args.dsl_file, args.output_name)
    except FileNotFoundError as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)
