"""Lists the texts of the settings window that go through T("...") (adjacent literals joined), so that
src/i18n.cpp (English) and src/i18n_uk.cpp (Ukrainian) can have them all. Prints the ones they do not have yet.

    python tools/extract_strings.py
"""
import os
import re

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(HERE, "src")

# T( then one or more "..." literals, possibly across lines.
CALL = re.compile(r'\b(?:T|N_)\(\s*((?:"(?:[^"\\]|\\.)*"\s*)+)\)')
LIT = re.compile(r'"((?:[^"\\]|\\.)*)"')


def texts():
    found = []
    for name in sorted(os.listdir(SRC)):
        if not name.endswith(".cpp") or name in ("i18n.cpp", "i18n_uk.cpp"):
            continue
        code = open(os.path.join(SRC, name), encoding="utf-8").read()
        for m in CALL.finditer(code):
            s = "".join(LIT.findall(m.group(1)))
            if re.search("[А-Яа-яЁё]", s) and s not in found:
                found.append(s)
    return found


def known(name="i18n.cpp"):
    path = os.path.join(SRC, name)
    if not os.path.exists(path):
        return set()
    code = open(path, encoding="utf-8").read()
    # { "key" "key continued", "English" }: the key may be several literals across lines.
    entry = re.compile(r'\{\s*((?:"(?:[^"\\]|\\.)*"\s*)+),')
    return {"".join(LIT.findall(m.group(1))) for m in entry.finditer(code)}


if __name__ == "__main__":
    all_texts = texts()
    for name, language in (("i18n.cpp", "English"), ("i18n_uk.cpp", "Ukrainian")):
        have = known(name)
        missing = [s for s in all_texts if s not in have]
        print(f"{len(all_texts)} texts, {len(missing)} without {language}")
        for s in missing:
            print(s)
