#!/usr/bin/env python3
"""Keep Settings' translation catalogs complete.

src/i18n/ar.json is the reference: its keys are every string Settings shows.
Every other catalog except en.json (which only carries English plural forms)
must have exactly those keys, keep each key's placeholders, and give each
counted string the plural forms its language uses.

    tools/i18n.py extract          source strings missing from ar.json, and
                                   ar.json keys no source uses any more
    tools/i18n.py check [lang...]  check catalogs (all of them by default)
    tools/i18n.py todo <lang>      print the keys <lang> still lacks, as JSON
"""
import glob
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
I18N = os.path.join(ROOT, "src", "i18n")
REFERENCE = "ar"

# The plural categories src/translator.cpp's pluralCategory() returns for
# whole counts, per language; "other" is always needed too (negative counts
# and missing forms fall back to it).
PLURALS = {
    **{c: {"other"} for c in ["ja", "ko", "zh-CN", "zh-TW", "th", "vi", "id", "ms"]},
    **{c: {"one", "other"} for c in ["hi", "bn", "fa", "si", "fil", "is", "he"]},
    **{c: {"one", "many", "other"} for c in ["fr", "pt-BR", "ca", "es-MX", "it", "pt"]},
    **{c: {"one", "few", "other"} for c in ["cs", "sk", "hr", "sr", "lt", "ro"]},
    **{c: {"one", "few", "many", "other"} for c in ["pl", "ru", "uk"]},
    "sl": {"one", "two", "few", "other"},
    "lv": {"zero", "one", "other"},
    "ar": {"zero", "one", "two", "few", "many", "other"},
    "ga": {"one", "two", "few", "many", "other"},
}
PLURALS["he"] = {"one", "two", "other"}


def plurals(lang):
    return PLURALS.get(lang, {"one", "other"}) | {"other"}


LIT = r'"((?:[^"\\]|\\.)*)"'


def unescape(s):
    """A QML or C++ string literal's text; escapes JSON lacks are kept as written."""
    escapes = {"n": "\n", "t": "\t", '"': '"', "'": "'", "\\": "\\"}

    def one(m):
        if m.group(1):
            return chr(int(m.group(1), 16))
        if m.group(2):
            return chr(int(m.group(2), 16))
        return escapes.get(m.group(3), m.group(0))

    return re.sub(r"\\u\{([0-9A-Fa-f]+)\}|\\u([0-9A-Fa-f]{4})|\\(.)", one, s)


def sources():
    files = glob.glob(os.path.join(ROOT, "src", "**", "*.qml"), recursive=True)
    files += glob.glob(os.path.join(ROOT, "src", "*.cpp")) + glob.glob(os.path.join(ROOT, "src", "*.h"))
    # Whole-line comments go, so examples in them don't count as strings.
    return {f: re.sub(r"(?m)^\s*//.*$", "", open(f, encoding="utf-8").read()) for f in files}


def source_strings(texts):
    """Every literal passed to qsTr, qsTranslate, or tr, and every setting label."""
    keys = set()
    for text in texts.values():
        for m in re.finditer(r"\bqsTr\(\s*" + LIT + r"(?:\s*,\s*" + LIT + r")?", text):
            keys.add(unescape(m.group(1)) + ("|" + unescape(m.group(2)) if m.group(2) else ""))
        for m in re.finditer(r"\b(?:qsTranslate|QT_TRANSLATE_NOOP)\(\s*" + LIT + r"\s*,\s*" + LIT, text):
            keys.add(unescape(m.group(2)))
        for m in re.finditer(r"(?<![\w.])tr\(\s*" + LIT, text):
            keys.add(unescape(m.group(1)))
        # Setting labels in the C++ registry, shown through qsTranslate("Setting", label).
        for m in re.finditer(r"\.label\s*=\s*([^;]+);", text):
            # Not the operands of a comparison (source == "ac" ? … : …).
            rhs = m.group(1)
            keys.update(unescape(x.group(1)) for x in re.finditer(LIT, rhs)
                        if not re.search(r"[=!]=\s*$", rhs[:x.start()]))
        for m in re.finditer(r'\{"label",\s*' + LIT, text):
            keys.add(unescape(m.group(1)))
        for m in re.finditer(r"\bcommandSetting\(\s*" + r"\s*,\s*".join([LIT] * 3), text):
            keys.add(unescape(m.group(3)))
        for m in re.finditer(r"\baddHypr\(\s*" + r"\s*,\s*".join([LIT] * 5), text):
            keys.add(unescape(m.group(5)))
    return keys


def quoted(key, texts):
    """Whether key appears as a whole string literal anywhere in the source:
    setting labels are plain literals in the C++ registry."""
    literal = '"' + key.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'
    return any(literal in text for text in texts.values())


def load(lang):
    with open(os.path.join(I18N, lang + ".json"), encoding="utf-8") as f:
        return json.load(f)


def placeholders(s):
    return sorted(re.findall(r"%L?\d|%n", s))


def problems(lang, catalog, reference):
    out = []
    missing = sorted(set(reference) - set(catalog))
    extra = sorted(set(catalog) - set(reference))
    if missing:
        out.append(f"{len(missing)} missing, e.g. {missing[:3]}")
    if extra:
        out.append(f"{len(extra)} not in {REFERENCE}.json, e.g. {extra[:3]}")
    need = plurals(lang)
    for key, value in catalog.items():
        if key not in reference:
            continue
        source = key.split("|")[0]
        if isinstance(reference[key], dict):
            if not isinstance(value, dict):
                out.append(f"{key!r}: needs plural forms {sorted(need)}")
                continue
            if set(value) != need:
                out.append(f"{key!r}: forms {sorted(value)}, needs {sorted(need)}")
            for form, text in value.items():
                if not isinstance(text, str) or not text.strip():
                    out.append(f"{key!r}[{form}]: empty")
                    continue
                # The zero, one, and two forms may spell the count out ("a day");
                # every other form must show it.
                spelled = form in ("zero", "one", "two")
                want = [p for p in placeholders(source) if p != "%n" or not spelled]
                have = [p for p in placeholders(text) if p != "%n" or not spelled]
                if want != have:
                    out.append(f"{key!r}[{form}]: placeholders {have}, needs {want}")
        else:
            if not isinstance(value, str) or not value.strip():
                out.append(f"{key!r}: empty")
            elif placeholders(value) != placeholders(source):
                out.append(f"{key!r}: placeholders {placeholders(value)}, needs {placeholders(source)}")
    return out


def main(argv):
    command = argv[1] if len(argv) > 1 else "check"
    reference = load(REFERENCE)
    if command == "extract":
        texts = sources()
        used = source_strings(texts)
        print("In the source but not in ar.json:")
        for k in sorted(used - set(reference)):
            print("  " + k)
        print("In ar.json but used nowhere:")
        for k in sorted(k for k in reference if k not in used and not quoted(k, texts)):
            print("  " + k)
        return 0
    if command == "todo":
        lang = argv[2]
        path = os.path.join(I18N, lang + ".json")
        have = load(lang) if os.path.exists(path) else {}
        print(json.dumps({k: reference[k] for k in sorted(reference) if k not in have}, ensure_ascii=False, indent=2))
        return 0
    if command == "check":
        langs = argv[2:] or sorted(os.path.basename(p)[:-5] for p in glob.glob(os.path.join(I18N, "*.json")))
        failed = False
        for lang in langs:
            if lang == "en":
                continue
            found = problems(lang, load(lang), reference)
            print(f"{lang}: " + ("ok" if not found else f"{len(found)} problems"))
            for p in found[:40]:
                print("  " + p)
            failed |= bool(found)
        return 1 if failed else 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
