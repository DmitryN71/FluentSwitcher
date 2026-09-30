"""Lists the texts of the settings window that go through T("...") (adjacent literals joined), so that
src/i18n.cpp can have them all. Prints the ones i18n.cpp does not have yet.

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
        if not name.endswith(".cpp") or name == "i18n.cpp":
            continue
        code = open(os.path.join(SRC, name), encoding="utf-8").read()
        for m in CALL.finditer(code):
            s = "".join(LIT.findall(m.group(1)))
            if re.search("[А-Яа-яЁё]", s) and s not in found:
                found.append(s)
    return found


def known():
    path = os.path.join(SRC, "i18n.cpp")
    if not os.path.exists(path):
        return set()
    code = open(path, encoding="utf-8").read()
    # { "key" "key continued", "English" }: the key may be several literals across lines.
    entry = re.compile(r'\{\s*((?:"(?:[^"\\]|\\.)*"\s*)+),')
    return {"".join(LIT.findall(m.group(1))) for m in entry.finditer(code)}


if __name__ == "__main__":
    have = known()
    all_texts = texts()
    missing = [s for s in all_texts if s not in have]
    print(f"{len(all_texts)} texts, {len(missing)} without English")
    for s in missing:
        print(s)
