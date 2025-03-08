# `iconv()` bug on macOS 14 (Sonoma) and 15 (Sequoia)

[macos\_iconv\_bug.c](macos_iconv_bug.c) is a program that convert a string "こんにちはABC世界XY" (U+3053 U+3093 U+306B U+3061 U+306F 'A' 'B' 'C' U+4E16 U+754C 'X' 'Y') from `UTF-32LE` to `SHIFT_JIS` (a legacy Japanese encoding).

Expeted to output bytes are: `0x82 0xb1` (こ) `0x82 0xf1` (ん) `0x82 0xc9` (に) `0x82 0xbf` (ち) `0x82 0xcd` (は) `0x41` (A) `0x42` (B) `0x43` (C)  `0x90 0xa2` (世) `0x8a 0x45` (界) `0x58` (X) `0x59` (Y).

On Linux (glibc) and macOS 13 (Ventura) it outputs the expeted result.

But on macOS 14 (Sonoma) and 15 (Sequoia) it outputs `0x82 0xb1` (こ) `0x82 0xf1` (ん) `0x82 0xc9` (に) `0x82 0xbf` (ち) `0x82 0xcd` (は) `0x41` (A) `0x90 0xa2` (世) `0x8a 0x45` (界) `0x58` (X) `0x59` (Y). Note that `0x42` (B) and `0x43` (C) are missing from the output.
