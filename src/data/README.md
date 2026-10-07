# Word lists

`words_ru.txt`, `words_en.txt` and `words_uk.txt` are the beginnings of words for the automatic layout switch in the middle of a
word (`src/WordStart.h`); they are built into `FluentSwitcher.exe` as resources (`src/res/res.rc`).

They are adapted from the 2018 Russian, English and Ukrainian frequency lists of
[FrequencyWords](https://github.com/hermitdave/FrequencyWords) by Hermit Dave, made from
[OpenSubtitles 2018](http://opus.nlpl.eu/OpenSubtitles2018.php): lower case, "ё" as "е", letters of the language
only, words of three letters and more met at least three times and known to the Windows dictionary of the
language. Ukrainian has no Windows dictionary on the computer the lists are made on, and its subtitles are about
40 % Russian: letters of the Ukrainian alphabet only, words met at least twice, without the Russian words (those of
the Russian list that are much rarer in the Ukrainian subtitles than in the Russian ones: что, когда - not так,
тебе) and without the words without a vowel (broken encodings). `*_common.txt` - the frequent words only (50 times,
Ukrainian 5). Sorted by bytes (UTF-8), one word a line.

License: [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/), as the original lists. Built by
`tools/corpus/make_wordlist.cmd` in the project folder (outside this repository) from `ru_full.txt`,
`en_full.txt` and `uk_full.txt` of FrequencyWords.
