# Diction Dictionary Format Specification

Diction is a modern HTML5-based dictionary format designed for:

- offline dictionaries
- multilingual dictionaries
- media-rich dictionaries
- scalable large dictionaries
- fast indexed lookup
- clean semantic HTML5 entries
- easy styling using CSS
- easy dictionary creation and conversion

Diction uses standard technologies:

- HTML5
- CSS
- JSON
- ZIP archives

This makes dictionaries easier to:

- create
- edit
- debug
- convert
- maintain
- extend
- inspect manually

Unlike older dictionary formats, Diction avoids inventing custom markup syntax whenever possible.

Instead, Diction uses normal web standards that most developers already understand.

---

# 1. What is a `.diction` File?

A `.diction` file is simply a ZIP archive with a different extension.

Example:

```text
Oxford.diction
```

can be renamed to:

```text
Oxford.zip
```

and opened using any ZIP application.

Inside the archive are:

- dictionary entries
- metadata
- stylesheets
- optional media
- optional fonts

---

# 2. Recommended Archive Structure

Diction archives should contain a top-level directory.

This avoids filename conflicts when multiple dictionaries are extracted into the same location.

Recommended structure:

```text
Oxford.diction
└── Oxford/
    ├── Oxford.html
    ├── style.css
    ├── meta.json
    ├── media/
    └── fonts/
```

Advantages:

- cleaner extraction
- easier organization
- easier debugging
- avoids filename collisions
- easier tooling
- safer manual inspection

---

# 3. Minimal Layout

Minimal layout is useful for:

- text-only dictionaries
- lightweight dictionaries
- online media references
- quick testing
- small downloads

Example:

```text
SimpleEnglish.diction
└── SimpleEnglish/
    ├── SimpleEnglish.html
    ├── style.css
    └── meta.json
```

No local media is bundled.

Images/audio/video may use online URLs.

Example:

```html
<img src="https://example.com/book.jpg">
```

Example:

```html
<audio controls>
  <source src="https://example.com/book.mp3">
</audio>
```

Minimal layout is ideal when:

- internet access is available
- dictionary size should remain small
- media is hosted externally

---

# 4. Rich Media Layout

Rich media layout is useful for:

- fully offline dictionaries
- pronunciation audio
- image dictionaries
- sign language/video dictionaries
- bundled fonts
- self-contained distribution

Example:

```text
Oxford.diction
└── Oxford/
    ├── Oxford.html
    ├── style.css
    ├── meta.json
    ├── media/
    │   ├── book.jpg
    │   ├── book.mp3
    │   └── intro.mp4
    └── fonts/
        ├── ipa.ttf
        └── serif.woff2
```

Advantages:

- fully offline
- portable
- self-contained
- reliable media loading
- no internet dependency

---

# 5. Optional Directories

Do NOT include empty directories.

Only include:

- `media/` if local media exists
- `fonts/` if custom fonts exist

Good:

```text
MyDictionary.diction
└── MyDictionary/
    ├── MyDictionary.html
    ├── style.css
    └── meta.json
```

Also good:

```text
MyDictionary.diction
└── MyDictionary/
    ├── MyDictionary.html
    ├── style.css
    ├── meta.json
    └── media/
```

Also good:

```text
MyDictionary.diction
└── MyDictionary/
    ├── MyDictionary.html
    ├── style.css
    ├── meta.json
    ├── media/
    └── fonts/
```

---

# 6. Naming Recommendations

The HTML filename is NOT fixed.

It does NOT need to be:

```text
dictionary.html
```

It may be:

```text
Oxford.html
```

or:

```text
Longman.html
```

or any other valid HTML filename.

Example:

```text
Oxford.diction
└── Oxford/
    ├── Oxford.html
```

This is completely valid.

---

# 7. Recommended Naming Style

Although any HTML filename is allowed, matching names are strongly recommended.

Recommended:

```text
Oxford.diction
└── Oxford/
    ├── Oxford.html
```

Advantages:

- easier organization
- easier debugging
- easier automation
- easier tooling
- less confusion

Not recommended:

```text
Oxford.diction
└── Oxford/
    ├── file123.html
```

because it becomes harder to understand later.

---

# 8. Main Files

| File | Purpose |
|---|---|
| meta.json | Dictionary metadata |
| *.html | Dictionary entries |
| style.css | Dictionary styling |
| media/ | Images/audio/video |
| fonts/ | Custom fonts |

---

# 9. Understanding `meta.json`

`meta.json` stores information ABOUT the dictionary.

This is called metadata.

Example:

```json
{
  "format": 1,
  "id": "oald_demo",
  "name": "Oxford Advanced Learner's Dictionary",
  "short_name": "OALD",
  "index_languages": ["en"],
  "content_languages": ["en"],
  "version": "1.0",
  "stylesheet": "style.css",
  "html": "Oxford.html"
}
```

---

# 10. Explanation of `meta.json` Fields

| Field | Meaning |
|---|---|
| format | Diction format version |
| id | Internal unique identifier |
| name | Full dictionary name |
| short_name | Short display name |
| index_languages | Searchable languages |
| content_languages | Definition languages |
| version | Dictionary version |
| stylesheet | CSS filename |
| html | Main HTML filename |

---

# 11. Why Use Arrays for Languages?

Older formats assume only:

- one source language
- one target language

Modern dictionaries may support:

- one-to-many
- many-to-one
- multilingual translations

So arrays are better.

Example:

```json
{
  "index_languages": ["en"],

  "content_languages": [
    "es",
    "zh",
    "ru",
    "hi",
    "bn"
  ]
}
```

Meaning:

English words map to:

- Spanish
- Chinese
- Russian
- Hindi
- Bangla

---

# 12. Example Multilingual Entry

```html
<article id="book" class="entry">

  <header>
    <h1 class="headword">book</h1>
  </header>

  <section class="translations">

    <div class="translation" lang="es">
      libro
    </div>

    <div class="translation" lang="zh">
      书
    </div>

    <div class="translation" lang="ru">
      книга
    </div>

    <div class="translation" lang="hi">
      किताब
    </div>

    <div class="translation" lang="bn">
      বই
    </div>

  </section>

</article>
```

---

# 13. Why Use `lang=""`?

HTML5 already supports language tagging.

Example:

```html
<div lang="hi">
```

Benefits:

- proper font fallback
- accessibility
- better rendering
- future text-to-speech support
- language-aware styling

---

# 14. Dictionary Entry Structure

Every entry should use:

```html
<article id="word">
```

The `id` becomes the searchable headword.

Example:

```html
<article id="book">
```

means the searchable word is:

```text
book
```

---

# 15. Recommended Semantic Classes

| Class | Meaning |
|---|---|
| entry | Entire dictionary article |
| headword | Main word/title |
| pronunciation | Pronunciation section |
| ipa | IPA text |
| pos-group | One part-of-speech section |
| pos | Part of speech |
| definitions | Definitions area |
| examples | Examples section |
| example | Single example sentence |
| etymology | Word origin/history |
| synonyms | Synonym section |
| antonyms | Antonym section |
| notes | Grammar/usage notes |
| translations | Translation section |
| translation | Single translation |

---

# 16. Example Complete Entry

```html
<article id="book" class="entry">

  <header>

    <h1 class="headword">book</h1>

  </header>

  <section class="pronunciation">

    <span class="ipa">/bʊk/</span>

  </section>

  <section class="etymology">

    From Old English boc.

  </section>

  <!-- noun -->

  <section class="pos-group">

    <span class="pos">n.</span>

    <div class="definitions">

      <ol>

        <li>notebook</li>

        <li>a written text book</li>

      </ol>

    </div>

  </section>

  <!-- verb -->

  <section class="pos-group">

    <span class="pos">v.</span>

    <div class="definitions">

      penalize by writing a challan

    </div>

  </section>

  <section class="examples">

    <blockquote class="example">

      He booked the driver for speeding.

    </blockquote>

  </section>

</article>
```

---

# 17. Why Use `<ol>` Sometimes?

Use:

- `<ol>` when meanings are numbered
- plain text for single meanings

Good:

```html
<ol>
  <li>notebook</li>
  <li>a written text book</li>
</ol>
```

Good:

```html
<div class="definitions">
  penalize by writing a challan
</div>
```

Avoid unnecessary numbering for single meanings.

---

# 18. Etymology Support

Example:

```html
<section class="etymology">

  From Old English boc.

</section>
```

Useful for:

- learner dictionaries
- historical dictionaries
- linguistic references

---

# 19. Synonyms and Antonyms

Example:

```html
<section class="synonyms">

  volume, publication

</section>

<section class="antonyms">

  ignorance

</section>
```

---

# 20. Using Images

```html
<img src="media/book.jpg" alt="Book">
```

---

# 21. Using Audio

```html
<audio controls>
  <source src="media/book.mp3">
</audio>
```

---

# 22. Using Video

```html
<video controls>
  <source src="media/intro.mp4">
</video>
```

---

# 23. Using Custom Fonts

Directory:

```text
fonts/
├── ipa.ttf
└── serif.woff2
```

CSS:

```css
@font-face {
  font-family: "IPA";
  src: url("fonts/ipa.ttf");
}

.ipa {
  font-family: "IPA";
}
```

---

# 24. Example Minimal Dictionary

## Structure

```text
SimpleEnglish.diction
└── SimpleEnglish/
    ├── SimpleEnglish.html
    ├── style.css
    └── meta.json
```

## meta.json

```json
{
  "format": 1,
  "id": "simple_en",
  "name": "Simple English Dictionary",
  "short_name": "SED",
  "index_languages": ["en"],
  "content_languages": ["en"],
  "version": "1.0",
  "stylesheet": "style.css",
  "html": "SimpleEnglish.html"
}
```

## style.css

```css
body {
  font-family: sans-serif;
}

.entry {
  margin-bottom: 1rem;
}

.headword {
  font-size: 2rem;
  font-weight: bold;
}
```

## SimpleEnglish.html

```html
<article id="book" class="entry">

  <header>
    <h1 class="headword">book</h1>
  </header>

  <section class="pos-group">

    <span class="pos">noun</span>

    <div class="definitions">

      <ol>
        <li>A written work.</li>
      </ol>

    </div>

  </section>

</article>
```

---

# 25. Example Rich Media Dictionary

## Structure

```text
Oxford.diction
└── Oxford/
    ├── Oxford.html
    ├── style.css
    ├── meta.json
    ├── media/
    │   ├── book.jpg
    │   └── book.mp3
    └── fonts/
        └── ipa.ttf
```

## meta.json

```json
{
  "format": 1,
  "id": "oald_demo",
  "name": "Oxford Advanced Learner's Dictionary",
  "short_name": "OALD",
  "index_languages": ["en"],
  "content_languages": ["en"],
  "version": "1.0",
  "stylesheet": "style.css",
  "html": "Oxford.html"
}
```

## style.css

```css
@font-face {
  font-family: "IPA";
  src: url("fonts/ipa.ttf");
}

body {
  font-family: sans-serif;
}

.entry {
  margin-bottom: 1rem;
  padding: 1rem;
  border: 1px solid #ccc;
}

.headword {
  font-size: 2rem;
  font-weight: bold;
}

.ipa {
  font-family: "IPA";
}
```

## Oxford.html

```html
<article id="book" class="entry">

  <header>
    <h1 class="headword">book</h1>
  </header>

  <section class="pronunciation">
    <span class="ipa">/bʊk/</span>
  </section>

  <section class="pos-group">

    <span class="pos">noun</span>

    <div class="definitions">

      <ol>
        <li>A written or printed work.</li>
      </ol>

    </div>

  </section>

  <img src="media/book.jpg" alt="Book">

  <audio controls>
    <source src="media/book.mp3">
  </audio>

</article>
```

---

# 26. How to Create a `.diction` File

## Step 1 — Create Directory

Example:

```text
Oxford/
```

---

## Step 2 — Add Dictionary Files

Example:

```text
Oxford/
├── Oxford.html
├── style.css
├── meta.json
├── media/
└── fonts/
```

Only include directories that are actually used.

---

## Step 3 — Create ZIP Archive

Recommended command:

```bash
zip -r Oxford.zip Oxford
```

This creates:

```text
Oxford.zip
```

with structure:

```text
Oxford.zip
└── Oxford/
    ├── Oxford.html
    ├── style.css
    ├── meta.json
```

---

## Step 4 — Rename ZIP to `.diction`

Rename:

```text
Oxford.zip
```

to:

```text
Oxford.diction
```

Example:

```bash
mv Oxford.zip Oxford.diction
```

Done.

---

# 27. Cross-References and Internal Linking

Diction supports dictionary cross-references using:

```html
<a href="entry://book">
  book
</a>
```

This is standard HTML5 because HTML allows custom URI schemes.

Applications should intercept:

```text
entry://...
```

and dynamically load dictionary entries.

---

## Phrase References

Example:

```html
<a href="entry://Expressions telling people to hurry up">
  ↑Expressions telling people to hurry up
</a>
```

Recommended practice:

- keep decorative symbols only in visible text
- keep URI lookup keys clean

Better:

```html
<a href="entry://Expressions telling people to hurry up">
  ↑Expressions telling people to hurry up
</a>
```

Avoid:

```html
<a href="entry://↑Expressions telling people to hurry up">
```

because symbols complicate lookup normalization.

---

## Cross-Dictionary References

Future implementations may support:

```html
<a href="entry://oald/book">
  book
</a>
```

Structure:

```text
entry://dictionary_id/headword
```

---

## Why Not Use `#book`?

Traditional HTML links like:

```html
<a href="#book">
```

assume the entire HTML document is loaded into the browser.

Diction dictionaries may contain:

- millions of entries
- huge HTML files
- on-demand rendering

Therefore custom entry lookup links are better suited for dictionary applications.

---

# 28. Additional Recommended HTML5 Features

Diction strongly encourages modern HTML5 features.

---

## MathML Support

Useful for:

- mathematics
- chemistry
- engineering
- scientific dictionaries
- educational dictionaries

Example:

```html
<math>
  <mrow>
    <mi>a</mi>
    <mo>+</mo>
    <mi>b</mi>
    <mo>=</mo>
    <mi>c</mi>
  </mrow>
</math>
```

MathML is part of HTML5 and should be supported whenever possible.

---

## Ruby Annotation Support

Very important for:

- Japanese
- Chinese
- pronunciation glosses
- furigana

Example:

```html
<ruby>
  漢
  <rt>かん</rt>
</ruby>
```

---

## SVG Support

Useful for:

- vector illustrations
- diagrams
- anatomy
- geometry
- scalable symbols

Example:

```html
<img src="media/triangle.svg">
```

Inline SVG is also valid.

---

## RTL Language Support

Important for:

- Arabic
- Urdu
- Persian
- Hebrew

Example:

```html
<div dir="rtl" lang="ar">
```

Always use proper:

- `dir`
- `lang`

for right-to-left languages.

---

## Tables

Useful for:

- grammar tables
- conjugation
- declension
- linguistic charts

Example:

```html
<table>
  <tr>
    <th>Singular</th>
    <th>Plural</th>
  </tr>

  <tr>
    <td>book</td>
    <td>books</td>
  </tr>
</table>
```

---

# 29. Security Model

Diction dictionaries are content packages, NOT executable web applications.

Applications should treat dictionaries as untrusted content.

---

## Recommended Allowed Features

- HTML5
- CSS
- MathML
- SVG
- audio
- video
- local media
- semantic markup

---

## Recommended Restricted Features

Applications should normally disable or sanitize:

- JavaScript
- remote scripts
- iframes
- embedded web applications
- arbitrary remote execution

This helps keep dictionaries:

- safer
- portable
- predictable

---

# 30. Accessibility Recommendations

Dictionary authors should use:

- semantic HTML
- proper headings
- `alt` text for images
- `lang` attributes
- proper table structure

Example:

```html
<img src="media/book.jpg" alt="Book illustration">
```

---

# 31. List Styling Support

Diction supports standard HTML5 ordered and unordered lists.

Dictionary creators should use semantic HTML lists together with CSS styling.

This avoids inventing custom numbering syntax.

---

## Ordered List Styles

Common dictionary numbering styles:

| Style | Example |
|---|---|
| decimal | 1. 2. 3. |
| decimal-paren | 1) 2) 3) |
| lower-alpha | a. b. c. |
| lower-alpha-paren | a) b) c) |
| upper-roman | I. II. III. |
| upper-alpha | A. B. C. |
| upper-alpha-paren | A) B) C) |
| lower-roman | i. ii. iii. |

---

## Decimal Style

```html
<ol class="decimal">

  <li>Meaning one</li>

  <li>Meaning two</li>

</ol>
```

---

## Roman Numeral Style

```html
<ol class="upper-roman">

  <li>Primary sense</li>

  <li>Secondary sense</li>

</ol>
```

---

## Upper Alphabetic Style

```html
<ol class="upper-alpha">

  <li>Main category</li>

  <li>Secondary category</li>

</ol>
```

---

## Upper Alphabetic Parenthesis Style

```html
<ol class="upper-alpha-paren">

  <li>Main category</li>

  <li>Secondary category</li>

</ol>
```

---

## Alphabetic Style

```html
<ol class="lower-alpha">

  <li>Submeaning</li>

  <li>Another submeaning</li>

</ol>
```

---

## Parenthesis Styles

Example:

```html
<ol class="decimal-paren">

  <li>First meaning</li>

  <li>Second meaning</li>

</ol>
```

Example:

```html
<ol class="lower-alpha-paren">

  <li>Example A</li>

  <li>Example B</li>

</ol>
```

---

## Example CSS

```css
ol.upper-roman {
  list-style-type: upper-roman;
}

ol.lower-alpha {
  list-style-type: lower-alpha;
}

ol.upper-alpha {
  list-style-type: upper-alpha;
}

ol.decimal {
  list-style-type: decimal;
}
```

---

## Upper Alphabetic Parenthesis CSS

```css
ol.upper-alpha-paren {
  list-style: none;
  counter-reset: item;
}

ol.upper-alpha-paren > li {
  counter-increment: item;
}

ol.upper-alpha-paren > li::before {
  content: counter(item, upper-alpha) ") ";
}
```

---

## Parenthesis Numbering CSS

```css
ol.decimal-paren {
  list-style: none;
  counter-reset: item;
}

ol.decimal-paren > li {
  counter-increment: item;
}

ol.decimal-paren > li::before {
  content: counter(item) ") ";
}
```

---

## Unordered Lists

Useful for:

- synonym groups
- feature lists
- semantic clusters
- classifications

Example:

```html
<ul class="dash-list">

  <li>x</li>

  <li>y</li>

  <li>z</li>

</ul>
```

Example CSS:

```css
ul.dash-list {
  list-style: none;
  padding-left: 1.2rem;
}

ul.dash-list li::before {
  content: "- ";
}
```

Result:

```text
- x
- y
- z
```

---

# 32. Dictionary Type Examples

---

## Monolingual Dictionary

Example:

English → English

```json
{
  "index_languages": ["en"],
  "content_languages": ["en"]
}
```

---

## One-to-Many Dictionary

Example:

English → Spanish + Chinese + Hindi

```json
{
  "index_languages": ["en"],

  "content_languages": [
    "es",
    "zh",
    "hi"
  ]
}
```

Example entry:

```html
<article id="book" class="entry">

  <header>
    <h1 class="headword">book</h1>
  </header>

  <section class="translations">

    <div class="translation" lang="es">
      libro
    </div>

    <div class="translation" lang="zh">
      书
    </div>

    <div class="translation" lang="hi">
      किताब
    </div>

  </section>

</article>
```

---

## Many-to-One Dictionary

Example:

Hindi + Urdu + Bangla → English

```json
{
  "index_languages": [
    "hi",
    "ur",
    "bn"
  ],

  "content_languages": ["en"]
}
```

Example entries:

```html
<article id="किताब" class="entry">

  <header>
    <h1 class="headword">किताब</h1>
  </header>

  <section class="translations">

    <div class="translation" lang="en">
      book
    </div>

  </section>

</article>
```

```html
<article id="کتاب" class="entry">

  <header>
    <h1 class="headword">کتاب</h1>
  </header>

  <section class="translations">

    <div class="translation" lang="en">
      book
    </div>

  </section>

</article>
```

---

## Many-to-Many Dictionary

Example:

Japanese + Chinese → English + Hindi

```json
{
  "index_languages": [
    "ja",
    "zh"
  ],

  "content_languages": [
    "en",
    "hi"
  ]
}
```

---

## Scientific Dictionary Example

Useful for:

- mathematics
- chemistry
- engineering
- physics

Example:

```html
<article id="pythagorean_theorem" class="entry">

  <header>
    <h1 class="headword">Pythagorean theorem</h1>
  </header>

  <math>
    <mrow>
      <msup>
        <mi>a</mi>
        <mn>2</mn>
      </msup>

      <mo>+</mo>

      <msup>
        <mi>b</mi>
        <mn>2</mn>
      </msup>

      <mo>=</mo>

      <msup>
        <mi>c</mi>
        <mn>2</mn>
      </msup>
    </mrow>
  </math>

</article>
```

---

## Japanese Ruby Annotation Example

```html
<article id="漢字" class="entry">

  <header>
    <h1 class="headword">
      <ruby>
        漢字
        <rt>かんじ</rt>
      </ruby>
    </h1>
  </header>

</article>
```

---

## RTL Dictionary Example

```html
<article id="كتاب" class="entry">

  <header>
    <h1 class="headword" dir="rtl" lang="ar">
      كتاب
    </h1>
  </header>

</article>
```

---

# 33. Feature Priority Recommendations

Most important modern Diction features:

| Priority | Feature |
|---|---|
| HIGH | MathML |
| HIGH | Ruby annotations |
| HIGH | RTL support |
| HIGH | Cross-reference/link scheme |
| HIGH | Security/script policy |
| HIGH | SVG |
| MEDIUM | metadata expansion |
| MEDIUM | accessibility guidance |
| MEDIUM | tables |
| LOW | citations |
| LOW | collapsible sections |

---

# 32. Important Design Philosophy

Diction is NOT a normal website format.

It is:

- an indexed dictionary format
- optimized for large dictionaries
- rendered dynamically by applications

Applications may:

- load only requested entries
- cache extracted HTML
- cache media
- perform indexing
- perform full-text search

This keeps huge dictionaries fast and memory-efficient.

Indexes and runtime cache are intentionally NOT stored inside `.diction` archives.

This keeps the format:

- portable
- implementation-independent
- cleaner
- future-proof

---

# 28. Final Recommended Structure

Recommended structure for most dictionaries:

```text
MyDictionary.diction
└── MyDictionary/
    ├── MyDictionary.html
    ├── style.css
    ├── meta.json
    ├── media/
    └── fonts/
```

Use:

- semantic HTML5
- metadata in `meta.json`
- CSS only for styling
- optional media/fonts only when needed
- matching names for clarity
- top-level dictionary directory inside archive

This keeps Diction dictionaries:

- clean
- portable
- scalable
- understandable
- future-proof

