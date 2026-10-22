# Languages

Each of the menu's languages is one C file here that fills in a
`LANG_STRINGS` (declared in `Ui/Lang.h`):

| File        | Language            | `LANG_ID`      |
| ----------- | ------------------- | -------------- |
| `En.c`      | English             | `LANG_EN`      |
| `ZhHans.c`  | Simplified Chinese  | `LANG_ZH_HANS` |

English is the reference: any string a language leaves out is shown in
English, so a partial translation still works.

## Adding a language

1. Copy `En.c` to a file named after the language's tag, such as `De.c`
   or `PtBr.c`, rename `gLangEn` to match (`gLangDe`), and translate the
   strings. Set `.Name` to the language's name in itself ("Deutsch").
   Keep the `{}`, `{0}` and `{1}` placeholders; `{0}` and `{1}` may swap
   places when the word order needs it.
2. In `Ui/Lang.h`, add an ID before `LANG_COUNT`. Stored settings use
   these numbers, so always add at the end.
3. In `Ui/Lang.c`, declare the new strings (`extern CONST LANG_STRINGS
   gLangDe;`) and add them to `mLangs`.
4. In `Core/Settings.h`, raise `SETTINGS_LANGUAGES` to the new count.
5. In `Sim/Main.c`, add the tag to `mLangTags` so `--lang` can pick it.

The build picks up every `.c` file here on its own. Characters outside
ASCII that the menu font (`Assets/montserrat.oablf`) lacks are taken from
`Assets/SourceHanSansSC-Normal.otf` by `Scripts/mkfont.py`, and the build
fails naming any character that font lacks too. Run `make sim` to render
the screens to PNG and check that the strings fit.

## Adding a string

Add a field to `LANG_STRINGS` in `Ui/Lang.h` (only `CONST CHAR8 *`
fields), give it English text in `En.c`, then translate it in the other
files. Every string the menu draws belongs in these files, so the CJK face
has its characters.
