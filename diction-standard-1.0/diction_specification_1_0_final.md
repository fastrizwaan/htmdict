# Diction Specification 1.0

Version: 1.0
Status: Final Specification

---

# Table of Contents

1. Introduction
2. Design Philosophy
3. Core Architecture
4. The `.diction` File
5. Archive Layout
6. Required Files
7. `meta.json` Specification
8. Fragment Stream Model
9. Entry Structure
10. Headwords
11. Aliases
12. Inflections
13. Senses
14. Definitions
15. Parts of Speech
16. Pronunciations
17. Examples
18. Etymology
19. Cross References
20. Semantic Elements
21. List and Numbering Conventions
22. Multilingual Dictionaries
23. Media and Fonts
24. HTML5 Compatibility
25. Styling Model
26. Parsing Requirements
27. Indexing Rules
28. Runtime Architecture
29. Tokenizer Guidance
30. Security Model
31. Accessibility
32. Packaging Guidelines
33. Examples
34. DSL → Diction Conversion
35. Reference Python Converter
36. Compliance Checklist
37. Versioning and Forward Compatibility

---

# 1. Introduction

Diction is an HTML5-inspired lexical markup and dictionary interchange format.

It is designed for:

- dictionary compilation
- lexical interchange
- structured linguistic storage
- high-performance lookup systems
- offline and online dictionary runtimes

Diction stores lexical content as structured fragment streams rather than standalone web pages.

A `.diction` package is:

- a lexical source container
- a compiler input format
- a semantic fragment stream
- an interchange representation

A `.diction` package is NOT:

- a standalone browser document
- a self-contained web application
- an EPUB-like runtime package
- a complete HTML website

---

# 2. Design Philosophy

## 2.1 Human Readability

Source files should remain inspectable and editable with ordinary text editors.

## 2.2 HTML5-Compatible Syntax

Diction uses HTML5 parsing principles and valid HTML-compatible tag syntax.

Its lexical elements behave as semantic containers rather than browser components.

## 2.3 Fragment-Oriented Design

Diction files are fragment streams.

They intentionally omit:

- `<html>`
- `<head>`
- `<body>`
- embedded runtime metadata
- browser application state

## 2.4 Streaming Efficiency

Diction is optimized for sequential tokenization and external indexing.

External indexes (byte-offset tables, sorted key arrays) are built by the runtime application from the fragment stream and stored in an application-managed cache. They are not bundled inside the `.diction` package. This mirrors the approach used by established dictionary runtimes such as GoldenDict and Lingvo, where index files are generated on first load and cached externally keyed by file path and modification time.

## 2.5 Unicode-First

All content is UTF-8 and fully multilingual.

## 2.6 Semantic Markup

Dictionary semantics are represented explicitly through dedicated lexical tags.

---

# 3. Core Architecture

```text
Diction fragment stream
            ↓
   Index compiler/tokenizer
   (run by the application on first load;
    index stored externally in app cache)
            ↓
   Flat binary lookup index
            ↓
      Runtime dictionary
            ↓
     Rendering/view layer
```

The `.diction` source package contains lexical fragments only. Index files are never bundled inside the package.

---

# 4. The `.diction` File

A `.diction` file is a ZIP archive using the `.diction` extension.

Example:

```text
Oxford.diction
```

Applications SHOULD treat `.diction` as a semantic package type.

## 4.1 Recommended MIME Types

```text
text/diction
text/html+diction
application/diction+zip
```

---

# 5. Archive Layout

```text
Oxford.diction
└── Oxford/
    ├── meta.json
    ├── Oxford.html
    ├── style.css
    ├── media/
    └── fonts/
```

Rules:

- `meta.json` is REQUIRED
- the HTML fragment file is REQUIRED
- `style.css` is REQUIRED
- `media/` is OPTIONAL
- `fonts/` is OPTIONAL

---

# 6. Required Files

| File | Purpose |
|---|---|
| `meta.json` | Package metadata |
| `*.html` | Fragment stream source |
| `style.css` | External rendering stylesheet |

---

# 7. `meta.json` Specification

```json
{
  "format": 1,
  "id": "oald_demo",
  "name": "Oxford Advanced Learner's Dictionary",
  "short_name": "OALD",
  "index_languages": ["en"],
  "content_languages": ["en"],
  "version": "1.0",
  "created": "2026-05-17",
  "stylesheet": "style.css",
  "html": "Oxford.html"
}
```

## 7.1 Field Definitions

| Field | Required | Description |
|---|---|---|
| `format` | REQUIRED | Integer. Must be `1` for this specification version. |
| `id` | REQUIRED | Stable machine identifier. Lowercase, alphanumeric, underscores and hyphens only. |
| `name` | REQUIRED | Full human-readable dictionary name. |
| `short_name` | REQUIRED | Abbreviated display name, max 20 characters. |
| `index_languages` | REQUIRED | BCP 47 list of headword languages. |
| `content_languages` | REQUIRED | BCP 47 list of definition/translation languages. |
| `version` | REQUIRED | Dictionary content version string (e.g. `"1.0"`). |
| `created` | REQUIRED | ISO 8601 date of package creation (e.g. `"2026-05-17"`). |
| `stylesheet` | REQUIRED | Filename of the CSS stylesheet. |
| `html` | REQUIRED | Filename of the HTML fragment stream. |

Diction uses BCP 47 language tags.

Examples:

```text
en
hi
ja
zh-Hans
zh-Hant
ar
ur
```

---

# 8. Fragment Stream Model

A Diction HTML file is NOT a complete HTML document.

It is a fragment stream.

The file MUST NOT contain:

- `<html>`
- `<head>`
- `<body>`
- `<script>`

Example:

```html
<article class="entry">
  <headword>apple</headword>
  <definition>A fruit.</definition>
</article>
```

---

# 9. Entry Structure

Every entry MUST use:

```html
<article class="entry">
</article>
```

Example:

```html
<article class="entry">
  <headword>water</headword>
  <pos>noun</pos>
  <definition>A transparent liquid.</definition>
</article>
```

Structural entries MAY omit `<headword>`.

Example:

```html
<article class="entry">
  <h2>Irregular Verbs</h2>
</article>
```

## 9.1 Recommended Entry Element Order

Within `<article class="entry">`, elements SHOULD appear in this order when present:

1. `<headword>`
2. `<alias>` (one or more)
3. `<inflection>` (one or more)
4. `<pronunciation>` (one or more)
5. `<pos>`
6. `<sense>` blocks (each containing definitions, examples, sub-senses)
7. `<etymology>`

This order is a recommendation, not a requirement. Applications MUST NOT depend on element order within an entry.

---

# 10. Headwords

The visible text content of `<headword>` defines the canonical key.

```html
<headword>apple pie</headword>
```

Unicode examples:

```html
<headword lang="hi">किताब</headword>
<headword lang="ja">漢字</headword>
<headword lang="ar" dir="rtl">كتاب</headword>
```

Markup stripping applies ONLY when generating temporary lookup keys.

Example:

```html
<headword>H<sub>2</sub>O</headword>
```

Indexed as:

```text
H2O
```

---

# 11. Aliases

Aliases define alternate lookup targets for the same entry.

```html
<article class="entry">
  <headword>colour</headword>
  <alias>color</alias>
</article>
```

Applications MUST index `<alias>` elements and resolve lookups against them in the same way as `<headword>` elements.

Multiple aliases are permitted:

```html
<headword>going to</headword>
<alias>gonna</alias>
<alias>going to (future)</alias>
```

---

# 12. Inflections

## 12.1 Purpose

`<inflection>` declares morphological forms of the headword that SHOULD be indexed as additional lookup keys pointing back to this entry.

This allows users to look up "ran", "running", or "tigers" and arrive at the canonical entry for "run" or "tiger" without requiring separate entries for every form.

## 12.2 Syntax

```html
<inflection form="FORM_NAME">SURFACE_FORM</inflection>
```

The `form` attribute SHOULD use a standardized grammatical label. Recommended values:

| `form` value | Meaning |
|---|---|
| `plural` | Plural form (nouns) |
| `plural-irregular` | Irregular plural |
| `past` | Simple past tense |
| `past-participle` | Past participle |
| `present-participle` | Present participle (-ing form) |
| `third-person-singular` | Third person singular present |
| `comparative` | Comparative adjective |
| `superlative` | Superlative adjective |
| `feminine` | Feminine gender form |
| `masculine` | Masculine gender form |
| `genitive` | Genitive/possessive form |
| `diminutive` | Diminutive form |
| `alternate` | Any other alternate spelling or form |

Applications MAY support additional `form` values. Unknown `form` values MUST NOT cause errors; the surface form SHOULD still be indexed.

## 12.3 Indexing Behavior

Applications MUST index each `<inflection>` text value as an alternate lookup key for the containing `<article class="entry">`.

Applications SHOULD display inflected lookups with a visual indication that the user is viewing the base entry, for example:

```text
tigers → tiger
```

## 12.4 Examples

**Regular noun:**

```html
<article class="entry">
  <headword>tiger</headword>
  <inflection form="plural">tigers</inflection>
  <pos>noun</pos>
  <definition>A large wild animal of the cat family.</definition>
</article>
```

**Irregular verb:**

```html
<article class="entry">
  <headword>run</headword>
  <inflection form="past">ran</inflection>
  <inflection form="past-participle">run</inflection>
  <inflection form="present-participle">running</inflection>
  <inflection form="third-person-singular">runs</inflection>
  <pos>verb</pos>
  <definition>To move swiftly on foot.</definition>
</article>
```

**Irregular noun:**

```html
<article class="entry">
  <headword>mouse</headword>
  <inflection form="plural-irregular">mice</inflection>
  <pos>noun</pos>
  <definition>A small rodent.</definition>
</article>
```

**Adjective with comparative and superlative:**

```html
<article class="entry">
  <headword>good</headword>
  <inflection form="comparative">better</inflection>
  <inflection form="superlative">best</inflection>
  <pos>adjective</pos>
  <definition>Of high quality or an excellent standard.</definition>
</article>
```

**Gendered forms (Romance languages):**

```html
<article class="entry">
  <headword lang="fr">acteur</headword>
  <inflection form="feminine">actrice</inflection>
  <inflection form="plural">acteurs</inflection>
  <inflection form="feminine-plural">actrices</inflection>
  <pos>nom</pos>
  <definition>Personne qui joue un rôle au théâtre ou au cinéma.</definition>
</article>
```

## 12.5 Relationship to `<alias>`

`<alias>` and `<inflection>` are both indexed as alternate lookup keys, but they serve different purposes:

| Element | Purpose | Example |
|---|---|---|
| `<alias>` | Alternate spelling or equivalent form of the headword itself | `colour` → `color` |
| `<inflection>` | Morphological variant with a grammatical relationship | `tiger` → `tigers` (plural) |

Applications MAY choose to display these differently (e.g. show the `form` attribute label when an inflected form was looked up).

---

# 13. Senses

## 13.1 Purpose

`<sense>` groups a coherent meaning of the headword, containing its definition, examples, domain labels, register labels, and any sub-senses.

Using `<sense>` allows applications to:

- display sense counts in search result previews
- collapse or expand individual senses
- link directly to a specific sense via `entry://word#sense-2`
- extract the first sense for summary display
- apply sense-level filtering (e.g. show only domain X)

## 13.2 Syntax

```html
<sense n="N">
  ...
</sense>
```

The `n` attribute is a positive integer identifying the sense number within the entry. Sense numbers SHOULD be sequential starting from 1. Applications MUST NOT assume senses are contiguous or that `n` values are gapless after round-tripping through editors.

## 13.3 Sense Attributes

| Attribute | Required | Description |
|---|---|---|
| `n` | REQUIRED | Sense number (positive integer). |
| `id` | OPTIONAL | Stable fragment identifier for deep-linking (e.g. `id="sense-1"`). |
| `domain` | OPTIONAL | Subject domain label (e.g. `"medicine"`, `"computing"`, `"law"`). |
| `register` | OPTIONAL | Register or usage label (e.g. `"formal"`, `"informal"`, `"archaic"`, `"slang"`). |
| `geo` | OPTIONAL | Geographic/dialect restriction (e.g. `"US"`, `"UK"`, `"AU"`). |

## 13.4 Sub-Senses

A `<sense>` MAY contain nested `<sense>` elements to represent sub-senses.

Sub-sense `n` values SHOULD use a dotted notation: `n="1.1"`, `n="1.2"`, etc.

## 13.5 Deep-Linking

When a `<sense>` carries an `id` attribute, it MAY be targeted by `entry://` links using a fragment identifier:

```html
<a href="entry://bank#sense-2">bank (financial institution)</a>
```

Applications SHOULD scroll or highlight the referenced sense when resolving such links.

## 13.6 Examples

**Simple multi-sense entry:**

```html
<article class="entry">
  <headword>bank</headword>
  <inflection form="plural">banks</inflection>
  <pos>noun</pos>

  <sense n="1" id="sense-1" domain="finance">
    <definition>A financial institution that accepts deposits and makes loans.</definition>
    <example>She deposited her savings at the bank.</example>
    <example>The bank approved his mortgage application.</example>
  </sense>

  <sense n="2" id="sense-2">
    <definition>The land alongside or sloping down to a river or lake.</definition>
    <example>They sat on the grassy bank of the river.</example>
  </sense>

  <sense n="3" id="sense-3" domain="aviation">
    <definition>A lateral tilt of an aircraft during a turn.</definition>
    <example>The pilot executed a steep bank to the left.</example>
  </sense>
</article>
```

**Multi-sense verb entry with sub-senses:**

```html
<article class="entry">
  <headword>run</headword>
  <inflection form="past">ran</inflection>
  <inflection form="past-participle">run</inflection>
  <inflection form="present-participle">running</inflection>
  <inflection form="third-person-singular">runs</inflection>
  <pos>verb</pos>

  <sense n="1" id="sense-1">
    <definition>To move swiftly on foot.</definition>
    <example>She runs five kilometres every morning.</example>

    <sense n="1.1">
      <definition>To move in this way as a sport or exercise.</definition>
      <example>He ran the marathon in under three hours.</example>
    </sense>

    <sense n="1.2">
      <definition>Of an animal: to move quickly using legs.</definition>
      <example>The deer ran across the field.</example>
    </sense>
  </sense>

  <sense n="2" id="sense-2" domain="computing">
    <definition>To execute a program or process.</definition>
    <example>Run the installer and follow the prompts.</example>
    <example>The script runs every night at midnight.</example>
  </sense>

  <sense n="3" id="sense-3" register="informal">
    <definition>To manage or be in charge of something.</definition>
    <example>She runs the entire department single-handedly.</example>
  </sense>
</article>
```

**Real dictionary entry — tiger:**

```html
<article class="entry">
  <headword>tiger</headword>
  <inflection form="plural">tigers</inflection>
  <alias>tigress</alias>

  <pronunciation dialect="GB">
    <span class="ipa">/ˈtaɪ.gər/</span>
    <a href="sound://media/tiger_gb.mp3">🔊</a>
  </pronunciation>
  <pronunciation dialect="US">
    <span class="ipa">/ˈtaɪ.gɚ/</span>
    <a href="sound://media/tiger_us.mp3">🔊</a>
  </pronunciation>

  <pos>noun</pos>

  <sense n="1" id="sense-1">
    <img src="media/tiger.jpg" alt="A Bengal tiger">
    <definition>A large wild animal of the cat family with yellowish-orange fur and black stripes, native to Asia.</definition>
    <example>We saw several tigers in the wild.</example>
    <example>The tiger is an endangered species.</example>
  </sense>

  <sense n="2" id="sense-2" register="informal">
    <definition>A fierce, determined, or ambitious person.</definition>
    <example>She's a tiger when it comes to negotiating contracts.</example>
  </sense>

  <etymology>From Latin <em>tigris</em>, from Greek <em>τίγρις</em>.</etymology>
</article>
```

---

# 14. Definitions

Definitions SHOULD use `<definition>`.

```html
<definition>A baked dessert.</definition>
```

`<definition>` elements MAY appear:

- directly inside `<article class="entry">` for simple single-sense entries
- inside `<sense>` blocks for multi-sense entries

Applications MUST treat `<definition>` elements in both positions equivalently for rendering purposes.

---

# 15. Parts of Speech

```html
<pos>noun</pos>
```

`<pos>` MAY appear at entry level (applying to all senses) or inside a `<sense>` (applying to that sense only).

When the same headword functions as multiple parts of speech, each should be a separate `<sense>`:

```html
<article class="entry">
  <headword>light</headword>

  <sense n="1">
    <pos>noun</pos>
    <definition>The natural agent that stimulates sight.</definition>
    <example>The light from the window was bright.</example>
  </sense>

  <sense n="2">
    <pos>adjective</pos>
    <definition>Having a small amount of weight.</definition>
    <example>The bag was surprisingly light.</example>
  </sense>

  <sense n="3">
    <pos>verb</pos>
    <inflection form="past">lit</inflection>
    <inflection form="past-participle">lit</inflection>
    <definition>To cause something to start burning.</definition>
    <example>She lit the candle.</example>
  </sense>
</article>
```

---

# 16. Pronunciations

## 16.1 Basic Usage

```html
<pronunciation>
  <span class="ipa">/bʊk/</span>
  <a href="sound://media/book.mp3">🔊</a>
</pronunciation>
```

## 16.2 Dialect Variants

`<pronunciation>` accepts a `dialect` attribute to distinguish regional variants.

Recommended `dialect` values use BCP 47 subtags or conventional abbreviations:

| Value | Meaning |
|---|---|
| `GB` | British English |
| `US` | American English |
| `AU` | Australian English |
| `CA` | Canadian English |
| `IRL` | Irish English |
| BCP 47 tag | Any regional variant (e.g. `fr-BE`, `es-MX`) |

```html
<pronunciation dialect="GB">
  <span class="ipa">/bʊk/</span>
  <a href="sound://media/book_gb.mp3">🔊</a>
</pronunciation>

<pronunciation dialect="US">
  <span class="ipa">/bʊk/</span>
  <a href="sound://media/book_us.mp3">🔊</a>
</pronunciation>
```

## 16.3 Phonetic Notation Classes

Authors SHOULD use standard class names for phonetic notation spans:

| Class | System |
|---|---|
| `ipa` | International Phonetic Alphabet |
| `respelling` | Pronunciation respelling |
| `pinyin` | Mandarin Chinese romanization |
| `romaji` | Japanese romanization |
| `transliteration` | Generic transliteration |

```html
<pronunciation>
  <span class="ipa">/ˈtaɪ.gər/</span>
</pronunciation>
```

## 16.4 Sense-Level Pronunciation

`<pronunciation>` MAY appear inside `<sense>` when pronunciation differs by meaning.

## 16.5 Accessibility

Pronunciation blocks SHOULD expose both readable phonetic text and accessible audio controls when audio is available.

```html
<pronunciation dialect="GB">
  <span class="ipa">/bʊk/</span>
  <audio controls aria-label="British English pronunciation">
    <source src="media/book_gb.mp3" type="audio/mpeg">
  </audio>
</pronunciation>
```

Applications SHOULD ensure pronunciation audio is keyboard-accessible and screen-reader accessible.

---

# 17. Examples

```html
<example>She opened the book.</example>
```

Examples MAY include source attribution:

```html
<example source="Jane Austen, Pride and Prejudice">
  It is a truth universally acknowledged.
</example>
```

---

# 18. Etymology

```html
<etymology>
  From Old English <em>bōc</em>, of Germanic origin.
</etymology>
```

---

# 19. Cross References

Use the `entry://` scheme.

```html
<a href="entry://apple">apple</a>
```

Cross-dictionary:

```html
<a href="entry://oald/book">book</a>
```

Sense-specific deep link:

```html
<a href="entry://bank#sense-2">bank (financial institution)</a>
```

## 19.1 Resolution Rules

Applications MUST normalize lookup targets before performing resolution.

Required normalization behavior:

- Unicode NFC normalization
- case-insensitive matching
- embedded markup stripping

## 19.1.1 Unicode NFC Normalization

Applications MUST normalize text using Unicode NFC before indexing or lookup.

## 19.1.2 Case-Insensitive Matching

Applications MUST perform case-insensitive matching unless explicitly overridden.

## 19.1.3 Embedded Markup Stripping

Applications MUST strip formatting markup when generating lookup keys.

## 19.1.4 Percent-Encoding Equivalence

Applications MUST treat percent-encoded and non-percent-encoded lexical targets as equivalent during `entry://` resolution.

Examples:

| URI | Equivalent Target |
|---|---|
| `entry://apple pie` | `apple pie` |
| `entry://apple%20pie` | `apple pie` |

Applications SHOULD normalize percent-encoded sequences before lookup resolution.

## 19.2 Optional Search Enhancements

Applications MAY implement:

- accent-insensitive matching
- locale-aware collation
- fuzzy matching
- Unicode-aware case folding

## 19.3 Cross-Dictionary Resolution Behavior

Applications SHOULD attempt to resolve cross-dictionary references using the dictionary identifier specified in the `entry://` target.

**Behavior when target dictionary is available:**

When the referenced dictionary is loaded and contains the target headword, applications SHOULD display the resolved entry in the same view as normal headword lookups.

**Behavior when target dictionary is unavailable:**

Applications SHOULD display a clear, user-friendly fallback message:

```text
Dictionary 'oald' not found.
```

Applications MUST NOT crash, throw errors, or execute any script when a cross-dictionary link cannot be resolved.

## 19.4 Media URI Schemes

Diction defines URI schemes for media playback without requiring JavaScript:

| Scheme | Purpose | Example |
|---|---|---|
| `sound://` | Play an audio file | `sound://media/pronunciation.mp3` |
| `video://` | Play a video file | `video://media/demonstration.mp4` |

These schemes are application-handled. Applications MUST confine media path resolution to files within the `media/` directory of the package. Path components that traverse outside this directory (e.g. `../`, absolute paths) MUST be rejected.

```html
<a href="sound://media/apple.mp3">🔊</a>
```

Standard `<audio>` and `<video>` elements MAY also be used directly:

```html
<audio controls>
  <source src="media/book.mp3" type="audio/mpeg">
</audio>

<video controls>
  <source src="media/demo.mp4" type="video/mp4">
</video>
```

If an application does not support `sound://` or `video://`, it SHOULD display the link as plain text or ignore it gracefully. Applications MUST NOT crash when encountering these schemes.

---

# 20. Semantic Elements

| Element | Meaning |
|---|---|
| `<article class="entry">` | Dictionary entry |
| `<headword>` | Canonical lookup key |
| `<alias>` | Alternate lookup key (spelling variant or equivalent) |
| `<inflection form="...">` | Morphological form (indexed as alternate lookup key) |
| `<sense n="...">` | Grouped meaning block |
| `<definition>` | Lexical definition |
| `<pos>` | Part of speech |
| `<pronunciation>` | Pronunciation block |
| `<example>` | Usage example |
| `<etymology>` | Historical origin |

---

# 21. List and Numbering Conventions

```html
<ol class="decimal">
  <li>A fruit.</li>
  <li>A computer manufacturer.</li>
</ol>
```

When `<sense>` elements are used, numbered lists inside senses SHOULD restart from 1.

---

# 22. Multilingual Dictionaries

```html
<div lang="hi">किताब</div>
<div lang="ar" dir="rtl">كتاب</div>
```

For bilingual dictionaries, `index_languages` and `content_languages` in `meta.json` will differ:

```json
{
  "index_languages": ["en"],
  "content_languages": ["fr"]
}
```

Headwords are in `index_languages`. Definitions and translations are in `content_languages`.

---

# 23. Media and Fonts

## 23.1 Images

```html
<img src="media/book.jpg" alt="Open hardcover book">
```

Images MUST be placed in the `media/` directory.

The `alt` attribute is REQUIRED on all `<img>` elements for accessibility.

For captioned images, use `<figure>` and `<figcaption>`:

```html
<figure>
  <img src="media/tiger.jpg" alt="A Bengal tiger in tall grass">
  <figcaption>Bengal tiger (<em>Panthera tigris tigris</em>)</figcaption>
</figure>
```

## 23.2 Video

Inline video SHOULD use the HTML5 `<video>` element:

```html
<video controls>
  <source src="media/demo.mp4" type="video/mp4">
  <source src="media/demo.webm" type="video/webm">
</video>
```

Video files MUST be placed in the `media/` directory.

## 23.3 Audio

```html
<audio controls>
  <source src="media/pronunciation.mp3" type="audio/mpeg">
</audio>
```

## 23.4 Fonts

Custom fonts MUST be placed in the `fonts/` directory and declared in `style.css`:

```css
@font-face {
  font-family: "IPA";
  src: url("fonts/ipa.woff2") format("woff2"),
       url("fonts/ipa.woff") format("woff");
}
```

Fonts SHOULD be subsetted to reduce package size where practical.

---

# 24. HTML5 Compatibility

Diction is an HTML5-compatible lexical fragment format.

Diction lexical tags are valid HTML-compatible custom elements:

```html
<headword>
<alias>
<inflection>
<sense>
<definition>
<pronunciation>
<example>
<etymology>
<pos>
```

These elements are semantic lexical containers. They are NOT Web Components, JavaScript custom elements, or executable browser modules.

## 24.1 Supported HTML5 Elements

Supported elements include:

- `<p>`, `<div>`, `<section>`, `<span>`
- `<strong>`, `<em>`, `<u>`, `<s>`, `<sub>`, `<sup>`
- `<br>`, `<abbr>`, `<dfn>`, `<code>`, `<kbd>`, `<var>`
- `<blockquote>`
- `<figure>`, `<figcaption>`
- `<table>`, `<thead>`, `<tbody>`, `<tr>`, `<th>`, `<td>`
- `<details>`, `<summary>`
- `<ruby>`, `<rt>`, `<rp>`
- `<audio>`, `<video>`, `<source>`
- `<img>`
- `<ol>`, `<ul>`, `<li>`
- `<h1>`–`<h6>`

## 24.2 MathML

```html
<math>
  <mrow>
    <msup><mi>a</mi><mn>2</mn></msup>
    <mo>+</mo>
    <msup><mi>b</mi><mn>2</mn></msup>
    <mo>=</mo>
    <msup><mi>c</mi><mn>2</mn></msup>
  </mrow>
</math>
```

## 24.3 SVG

Diction supports inline and external SVG graphics.

## 24.4 Ruby Annotations

```html
<ruby>漢<rt>かん</rt></ruby>
```

## 24.5 Content Placement Rules

Applications SHOULD tolerate valid HTML5 structural nesting inside lexical containers.

---

# 25. Styling Model

Styling is external.

Applications MAY render Diction fragments using:

- external CSS stylesheets declared in `meta.json`
- built-in semantic fallback styles
- native UI rendering systems

## 25.1 External Stylesheets

Applications SHOULD load the stylesheet declared in `meta.json`.

## 25.2 Built-In Fallback Rendering

Applications SHOULD provide a minimal built-in fallback stylesheet for when the package stylesheet cannot be loaded.

## 25.3 Sense Styling Guidance

Applications SHOULD style `<sense>` blocks so that individual senses are visually distinguishable. Recommended approaches:

- numbered sense labels derived from the `n` attribute
- subtle background shading or left border on each sense block
- collapsible sense blocks in interactive environments

## 25.4 Inflection Display Guidance

When a user looks up an inflected form, applications SHOULD indicate the redirection, for example by displaying a banner such as:

```
Showing entry for "tiger" (searched: "tigers")
```

## 25.5 Example CSS

```css
article.entry {
  margin-bottom: 1.5rem;
}

headword {
  display: block;
  font-size: 2rem;
  font-weight: bold;
}

sense {
  display: block;
  margin: 0.5rem 0 0.5rem 1rem;
  padding-left: 0.5rem;
  border-left: 2px solid #ddd;
}

sense[n]::before {
  content: attr(n) ". ";
  font-weight: bold;
  color: #555;
}

inflection {
  display: none; /* inflections are index-only; hide from rendered output */
}

definition {
  display: block;
  margin: 0.3rem 0;
}

example {
  display: block;
  margin: 0.4rem 0 0.4rem 1.5rem;
  font-style: italic;
  color: #555;
}

etymology {
  display: block;
  margin-top: 0.8rem;
  font-size: 0.9em;
  color: #666;
}

pronunciation {
  display: inline-block;
  margin-right: 0.5rem;
}
```

---

# 26. Parsing Requirements

Applications MUST implement structural parsing.

Applications MUST NOT depend on:

- literal whitespace
- attribute order
- exact formatting layout
- static regex-only parsing

---

# 27. Indexing Rules

Applications MUST strip markup inside:

- `<headword>`
- `<alias>`
- `<inflection>`

Applications MUST normalize all keys using Unicode NFC before indexing.

Applications MUST index `<headword>` as the canonical entry key.

Applications MUST index `<alias>` values as alternate keys pointing to the same entry.

Applications MUST index `<inflection>` values as alternate keys pointing to the same entry. The `form` attribute SHOULD be stored alongside the indexed key so applications can display the grammatical relationship on lookup.

Applications SHOULD use:

- stable sorting
- case-insensitive comparison
- normalized Unicode ordering

---

# 28. Runtime Architecture

Recommended runtime pipeline:

```text
fragment stream
      ↓
stream tokenizer
      ↓
external index (stored in app cache, not in .diction package)
      ↓
memory-mapped lookup
      ↓
entry extraction
      ↓
runtime rendering
```

## 28.1 External Indexing

Indexes are NOT bundled inside `.diction` packages. Applications build and cache indexes independently, typically keyed by the package file path and its modification timestamp or content hash. This mirrors established practice in dictionary runtimes such as GoldenDict.

## 28.2 Large Dictionary Sources

Diction is designed to support very large lexical fragment streams.

A single HTML fragment file MAY contain millions of entries. Applications SHOULD avoid assumptions that the entire fragment stream can be fully materialized in memory.

Recommended strategies include:

- streaming tokenization
- mmap-based access
- incremental indexing
- byte-offset lookup tables

## 28.3 Authoring Workflow for Large Dictionaries (Non-Normative)

During authoring, dictionary builders MAY maintain entries across multiple smaller source files. A build step SHOULD concatenate these into the final single HTML file before packaging.

```text
source fragments (many small files)
      ↓
build/concatenation step
      ↓
single dictionary.html
      ↓
.diction package
```

Applications processing packaged `.diction` archives MUST support only the single-file format.

---

# 29. Tokenizer Guidance

Tokenizers SHOULD:

- scan sequentially
- avoid DOM materialization
- minimize lookahead

Recommended indexing flow:

```text
find <article class="entry">
    ↓
find <headword>  → record as canonical key + byte offset
    ↓
find <alias>     → record as alternate key → same byte offset
    ↓
find <inflection form="..."> → record as alternate key + form label → same byte offset
    ↓
build index
```

---

# 30. Security Model

Diction packages are content containers, not executable applications.

Allowed:

- HTML5 structure
- CSS
- MathML
- SVG
- audio/video
- images
- `entry://` links
- `sound://` and `video://` links (confined to `media/` directory)

Restricted:

| Feature | Action |
|---|---|
| `<script>` | remove |
| `javascript:` URIs | strip |
| inline JS event attributes (`onclick`, etc.) | strip |
| `<iframe>` | remove |
| `<form>` | disable |
| media paths traversing outside `media/` | reject |

Applications MUST NOT execute embedded JavaScript.

## 30.1 Media Path Confinement

Applications resolving `sound://`, `video://`, `src` attributes on `<img>`, `<audio>`, and `<video>` elements MUST verify that the resolved file path remains within the `media/` directory of the package.

Path components including `..`, absolute paths, and URL-encoded equivalents (e.g. `%2F`, `%2E%2E`) MUST be rejected before any file access is attempted.

Example rejected paths:

```text
sound://media/../../../etc/passwd   → REJECT
sound://media/%2e%2e/secret.mp3    → REJECT
sound://media/pronunciation.mp3    → ALLOW
```

---

# 31. Accessibility

## 31.1 Images

The `alt` attribute is REQUIRED on all `<img>` elements:

```html
<img src="media/book.jpg" alt="Open hardcover book">
```

## 31.2 Language Tags

```html
<div lang="hi">किताब</div>
```

## 31.3 RTL Support

```html
<div lang="ar" dir="rtl">كتاب</div>
```

## 31.4 Pronunciation Accessibility

Pronunciation blocks SHOULD expose both readable phonetic text and accessible audio controls.

```html
<pronunciation dialect="GB">
  <span class="ipa">/bʊk/</span>
  <audio controls aria-label="British English pronunciation">
    <source src="media/book_gb.mp3" type="audio/mpeg">
  </audio>
</pronunciation>
```

## 31.5 Sense Navigation

Applications SHOULD provide keyboard navigation between senses within an entry.

---

# 32. Packaging Guidelines

```bash
mkdir -p Oxford/media Oxford/fonts
# copy HTML, style.css, meta.json, media files
zip -r Oxford.diction Oxford/
```

Binary and media files (images, audio, video, fonts) SHOULD be stored uncompressed (`STORED`) in the ZIP since they are already compressed. Text files (HTML, CSS, JSON) SHOULD use `DEFLATE` compression.

---

# 33. Examples

## 33.1 Minimal Entry

```html
<article class="entry">
  <headword>apple</headword>
  <inflection form="plural">apples</inflection>
  <pos>noun</pos>
  <definition>A round fruit with red, green, or yellow skin.</definition>
</article>
```

## 33.2 Full Multi-Sense Entry

```html
<article class="entry">
  <headword>light</headword>
  <inflection form="plural">lights</inflection>
  <inflection form="comparative">lighter</inflection>
  <inflection form="superlative">lightest</inflection>

  <pronunciation dialect="GB">
    <span class="ipa">/laɪt/</span>
    <a href="sound://media/light_gb.mp3">🔊</a>
  </pronunciation>

  <sense n="1" id="sense-1">
    <pos>noun</pos>
    <definition>The natural agent that stimulates sight and makes things visible.</definition>
    <example>Sunlight streamed through the windows.</example>
    <example>She switched on the light.</example>
  </sense>

  <sense n="2" id="sense-2">
    <pos>adjective</pos>
    <definition>Having little weight; not heavy.</definition>
    <example>The package was surprisingly light.</example>

    <sense n="2.1" register="informal">
      <definition>Not dense, serious, or difficult.</definition>
      <example>Something light to read on holiday.</example>
    </sense>
  </sense>

  <sense n="3" id="sense-3">
    <pos>verb</pos>
    <inflection form="past">lit</inflection>
    <inflection form="past-participle">lit</inflection>
    <definition>To cause something to start burning or to begin to burn.</definition>
    <example>He lit the candles on the table.</example>
  </sense>

  <etymology>Old English <em>lēoht</em>, of Germanic origin.</etymology>
</article>
```

## 33.3 Scientific Entry

```html
<article class="entry">
  <headword>Pythagorean theorem</headword>
  <alias>Pythagoras theorem</alias>
  <pos>noun</pos>
  <sense n="1" domain="mathematics">
    <definition>
      The theorem stating that in a right-angled triangle, the square of the
      hypotenuse equals the sum of the squares of the other two sides:
      <math>
        <mrow>
          <msup><mi>a</mi><mn>2</mn></msup>
          <mo>+</mo>
          <msup><mi>b</mi><mn>2</mn></msup>
          <mo>=</mo>
          <msup><mi>c</mi><mn>2</mn></msup>
        </mrow>
      </math>
    </definition>
  </sense>
</article>
```

## 33.4 Bilingual Entry (English–French)

```html
<article class="entry">
  <headword lang="en">cat</headword>
  <inflection form="plural">cats</inflection>
  <pos>noun</pos>

  <sense n="1">
    <definition lang="fr">chat <em>(m)</em></definition>
    <example lang="en">The cat sat on the mat.</example>
    <example lang="fr">Le chat est sur le tapis.</example>
  </sense>
</article>
```

## 33.5 Japanese Entry with Ruby

```html
<article class="entry">
  <headword lang="ja">漢字</headword>
  <pronunciation>
    <span class="romaji">kanji</span>
    <span class="ipa">/kandʑi/</span>
  </pronunciation>
  <pos>名詞</pos>
  <sense n="1">
    <definition>
      Chinese characters used in Japanese writing:
      <ruby>漢<rt>かん</rt></ruby><ruby>字<rt>じ</rt></ruby>
    </definition>
  </sense>
</article>
```

---

# 34. DSL → Diction Conversion

Many legacy dictionaries use ABBYY Lingvo DSL format.

Recommended mapping:

| DSL | Diction |
|---|---|
| Headword line | `<headword>` |
| Multiple headword lines (same entry) | `<alias>` |
| `[m0]`–`[m9]` | `<definition class="m0">`–`<definition class="m9">` |
| `[i]` | `<em>` |
| `[b]` | `<strong>` |
| `[u]` | `<u>` |
| `[s]` + audio extension | `<a href="sound://media/...">🔊</a>` |
| `[s]` + image extension | `<img src="media/..." alt="...">` |
| `[s]` + video extension | `<video src="media/..." controls>` |
| `[video]` | `<video src="media/..." controls>` |
| `[img]` | `<img src="media/..." alt="...">` |
| `[ex]` | `<example>` |
| `[p]` | `<pos>` |
| `[t]` | `<span class="ipa">` |
| `[ref]word[/ref]` | `<a href="entry://word">` |
| `<<word>>` | `<a href="entry://word">` |
| `[url]` | `<a href="...">` |
| `[c color]` | `<span class="colored" style="color:...">` |
| `[sup]` / `[sub]` | `<sup>` / `<sub>` |
| `[abr]` / `[abbr]` | `<abbr class="dsl">` |
| `[com]` | `<span class="comment">` |
| `[trn]` / `[!trs]` | `<span class="translation">` |
| `[lang id=xx]` | `<span lang="xx">` |
| `[*]...[/*]` | `<span class="opt">` (hidden) |
| `{{tag}}...{{/tag}}` | strip tags, keep content |
| `#INDEX_LANGUAGE` | `meta.json` `index_languages` (via BCP 47 map) |
| `#CONTENTS_LANGUAGE` | `meta.json` `content_languages` (via BCP 47 map) |
| `#NAME` | `meta.json` `name` + HTML header comment |

Note: DSL does not have native sense or inflection markup. These elements should be populated by dictionary authors or post-processing tools when converting DSL to Diction.

---

# 35. Reference Python Converter

The specification includes a reference converter architecture for transforming DSL dictionaries into Diction fragment streams.

## 35.1 Reference Converter Limitations

The included Python converter is a minimal educational reference implementation.

It is intentionally simplified and may not correctly handle:

- deeply nested DSL markup
- multiline semantic blocks
- malformed legacy DSL
- complex embedded formatting
- advanced Lingvo directives
- automatic sense or inflection detection (these must be added manually or via post-processing)

Production-grade converters SHOULD use a proper parser architecture rather than simple line-oriented processing.

Recommended approaches include:

- tokenizer/parser pipelines
- recursive descent parsing
- parser combinators
- libraries such as `lark` or `pyparsing`

---

# 36. Compliance Checklist

### Archive

- [ ] valid ZIP archive
- [ ] single root directory inside the archive
- [ ] `meta.json` present and valid
- [ ] HTML fragment file present (filename matches `meta.json` `"html"` field)
- [ ] `style.css` present (filename matches `meta.json` `"stylesheet"` field)
- [ ] all files encoded as UTF-8
- [ ] `meta.json` includes `"created"` field

### Fragment Stream

- [ ] no `<html>` wrapper
- [ ] no `<head>` section
- [ ] no `<body>` section
- [ ] no `<script>` elements
- [ ] all `<img>` elements have `alt` attributes

### Entry Structure

- [ ] every entry uses `<article class="entry">`
- [ ] `<inflection>` elements carry a `form` attribute
- [ ] `<sense>` elements carry an `n` attribute
- [ ] `<sense>` `n` values are unique within each entry
- [ ] `<pronunciation>` elements use `dialect` attribute when multiple variants are present

### Entry Ordering

- [ ] entries sorted alphabetically by headword (case-insensitive, Unicode NFC normalized)

### Indexing

- [ ] `<headword>` indexed as canonical key
- [ ] `<alias>` values indexed as alternate keys
- [ ] `<inflection>` values indexed as alternate keys with `form` label stored

### Security

- [ ] no JavaScript
- [ ] no executable payloads
- [ ] no inline JS event attributes
- [ ] media paths confined to `media/` directory (no path traversal)

---

# 37. Versioning and Forward Compatibility

This document defines:

```text
Diction Specification 1.0
```

## 37.1 Forward Compatibility

Future versions SHOULD preserve backward compatibility whenever practical.

Applications MUST ignore unknown:

- tags
- attributes
- classes
- metadata extensions

without crashing.
