# Word lists

`words_ru.txt` and `words_en.txt` are the beginnings of words for the automatic layout switch in the middle of a
word (`src/WordStart.h`); they are built into `FluentSwitcher.exe` as resources (`src/res/res.rc`).

They are adapted from the 2018 Russian and English frequency lists of
[FrequencyWords](https://github.com/hermitdave/FrequencyWords) by Hermit Dave, made from
[OpenSubtitles 2018](http://opus.nlpl.eu/OpenSubtitles2018.php): lower case, "ё" as "е", letters of the language
only, words of three letters and more met at least three times and known to the Windows dictionary of the
language. Sorted by bytes (UTF-8), one word a line.

License: [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/), as the original lists. Built by
`tools/corpus/make_wordlist.cmd` in the project folder (outside this repository) from `ru_full.txt` and
`en_full.txt` of FrequencyWords.
