# `iconv()` bug on macOS 14 (Sonoma) and 15 (Sequoia)

[macos\_iconv\_bug.c](macos_iconv_bug.c) is a program that convert a string "こんにちはABC世界XY" (`U+3053` `U+3093` `U+306B` `U+3061` `U+306F` 'A' 'B' 'C' `U+4E16` `U+754C` 'X' 'Y') from `UTF-32LE` to `SHIFT_JIS` (a legacy Japanese encoding).

Expected output bytes are: `0x82 0xb1` (こ) `0x82 0xf1` (ん) `0x82 0xc9` (に) `0x82 0xbf` (ち) `0x82 0xcd` (は) `0x41` (A) `0x42` (B) `0x43` (C)  `0x90 0xa2` (世) `0x8a 0x45` (界) `0x58` (X) `0x59` (Y).

It outputs the expected bytes on Linux (Ubuntu Linux 22.04) and macOS 13 (Ventura).

But on macOS 14 (Sonoma) and 15 (Sequoia) it outputs `0x82 0xb1` (こ) `0x82 0xf1` (ん) `0x82 0xc9` (に) `0x82 0xbf` (ち) `0x82 0xcd` (は) `0x41` (A) `0x90 0xa2` (世) `0x8a 0x45` (界) `0x58` (X) `0x59` (Y). Note that `0x42` (B) and `0x43` (C) are missing from the output.

I noticed this problem by the CI failure of my `bytestring-encoding` package for Haskell ([issue](https://github.com/msakai/bytestring-encoding/issues/15)).

## How to test

On macOS:

```console
$ gcc macos_iconv_bug.c -liconv -o macos_iconv_bug
$ ./macos_iconv_bug
```

On Linux:

```console
$ gcc macos_iconv_bug.c -o macos_iconv_bug
$ ./macos_iconv_bug
```

After the execution, `PASS` or `FAIL` is printed, and the output bytes are saved in the `result.txt` file.

## Result on GitHub Actions 

I prepared [a GitHub Actions workflow](.github/workflows/test.yaml) to run the test program. The result is as follows:

|OS|Result|
|-|-|
|macOS 13 (Ventura)|✅ PASS|
|macOS 14 (Sonoma)|❌ FAIL|
|macOS 15 (Sequoia)|❌ FAIL|
|macOS 26 (Tahoe)|✅ PASS|
|Ubuntu Linux 22.04|✅ PASS|
|FreeBSD 13.4|✅ PASS|
|FreeBSD 14.2|✅ PASS|
|FreeBSD 15.0|✅ PASS|
|NetBSD 9.4|✅ PASS|
|NetBSD 10.1|✅ PASS|
|Dragonfly BSD 6.4.0|✅ PASS|


## Versions of `libiconv`

According to the [Apple Open Source](https://opensource.apple.com/releases/) page,
- macOS 13.5 uses [libiconv-64](https://github.com/apple-oss-distributions/libiconv/tree/libiconv-64)
- macOS 14.0 uses [libiconv-80.1.1](https://github.com/apple-oss-distributions/libiconv/tree/libiconv-80.1.1) ([diff](https://github.com/apple-oss-distributions/libiconv/compare/libiconv-64...libiconv-80.1.1))
- macOS 15.0 uses [libiconv-107](https://github.com/apple-oss-distributions/libiconv/tree/libiconv-107) ([diff](https://github.com/apple-oss-distributions/libiconv/compare/libiconv-80.1.1...libiconv-107))
- macOS 26.0 uses [libiconv-113](https://github.com/apple-oss-distributions/libiconv/tree/libiconv-113) ([diff](https://github.com/apple-oss-distributions/libiconv/compare/libiconv-107...libiconv-113))

## Root cause

> The following analysis was produced by Claude Code (Anthropic's Opus 4.8 model) by reading the `libiconv-107` (buggy) and `libiconv-113` (fixed) source trees.

### Summary

The bug is a **one-line mistake in Apple's own, macOS-only "batched" conversion code**: a function reported how many *input characters* it had consumed but failed to report how many *output bytes* it had actually written when it stopped early on `E2BIG`. The caller then advanced the input pointer while leaving the output pointer untouched, silently discarding bytes (`B` and `C`) that had already been written into the output buffer.

It was fixed in `libiconv-113` by [a single-line change](https://github.com/apple-oss-distributions/libiconv/compare/libiconv-107...libiconv-113) in [`libiconv_modules/iconv_std/citrus_iconv_std.c`](https://github.com/apple-oss-distributions/libiconv/blob/libiconv-113/libiconv_modules/iconv_std/citrus_iconv_std.c):

```diff
-		if (ret == 0)
-			*nresult = acc;
+		*nresult = acc;
```

### What goes wrong

The decisive step is the **second** `iconv()` call in the test: it converts `"BC世界XY"` (UTF-32LE) into an output buffer that has only **3 bytes** left.

- Expected: write `B` (`0x42`) and `C` (`0x43`), leaving 1 byte; `世` needs 2 bytes → `E2BIG`. So `src` advances by 8 bytes and `dst` advances by 2 bytes.
- Buggy (macOS 14/15): `=> ret=-1 errno=7 src_left=16 dst_left=3`. The `src` pointer advanced by 8 bytes (consuming `B` and `C`), **but `dst` did not advance at all**. The `B`/`C` bytes that were already placed in the buffer are abandoned, so they are missing from the final result.

### Why (the code)

Starting with macOS 14, Apple rewrote the conversion loop to process **up to `_ICONV_STD_PERCVT` (= 32) characters per pass** for performance (the `csid[]` / `idx[]` / `delta[]` arrays in `_citrus_iconv_std_iconv_convert()`). Legacy multibyte target encodings such as SHIFT_JIS (`MSKanji`) do not implement the batched `cstombn` operation, so `cstombx()` falls back to a hand-written per-character loop. In `libiconv-107` that loop looked like this:

```c
acc = 0;
for (int i = 0; i < *cnt; i++) {
    ret = _stdenc_cstomb(..., csid[i], idx[i], ..., &tmp, ...);
    if (ret != 0) {        // E2BIG on 世 (needs 2 bytes, only 1 left)
        *cnt = i;          //  *cnt = 2  → reports "B and C succeeded"
        break;
    }
    acc += tmp; s += tmp; n -= tmp;   // B and C are physically written here (acc = 2)
    ...
}
if (ret == 0)              // ret == E2BIG → false
    *nresult = acc;        // ← SKIPPED, so the output byte count stays 0
```

The caller then trusts `*cnt` (2 characters consumed) and advances the **input** pointer by `delta[1]` = 8 bytes, but because `*nresult` (`szrout`) is still 0 it does **not** advance the **output** pointer. The two already-written bytes are lost. This mismatch between "input consumed" and "output produced" is exactly the observed `src_left=16 / dst_left=3`.

The fix makes the loop always report `acc`, even when it stops on an error, so input and output accounting stay consistent.

### Why the same Citrus-derived `libiconv` was fine on the other BSDs

1. **The batching is Apple-only.** The upstream NetBSD/FreeBSD Citrus code converts **one character at a time**; the whole batched path (the `_ICONV_STD_PERCVT` arrays, `cstombn`, and the buggy fallback loop) lives under `#ifdef __APPLE__`. On the non-Apple side, `cstombx()` is just a single call to `_stdenc_cstomb()`. The buggy loop simply does not exist there, which is why FreeBSD 13–15, NetBSD 9–10, and DragonFly BSD all PASS.

2. **One-at-a-time conversion is inherently atomic.** Upstream commits the input-consumed and output-written amounts together for each character; on `E2BIG` it jumps to error handling *before* committing that character, restoring state and advancing neither pointer. So when `世` does not fit, `B` and `C` were already committed in their own earlier iterations and can never be lost. Apple's "commit up to 32 characters at once" rewrite introduced the dual-pointer bookkeeping whose error path had the missing line.

3. **It only bites legacy multibyte target encodings.** The generic `stdenc_cstombn` template *does* set `*nresult` unconditionally, but it is only enabled for encodings that define `_ENCODING_HAVE_MBTOCSN`, i.e. **UTF8 / UTF8MAC / UTF1632**. SHIFT_JIS, EUC-JP, Big5, GBK, etc. lack it, so they return `EOPNOTSUPP` and hit the buggy hand-written fallback. Converting *to* UTF-8/16/32 would not trigger the bug — which is why it surfaced for Japanese (Shift_JIS) users in particular.

In short, three conditions must coincide to trigger it: **(a)** Apple's batched path, **(b)** a target encoding without a `cstombn` implementation, and **(c)** the output buffer filling up partway through a batch (an `E2BIG` *after* at least one character has been written). This matches the real-world pattern of a fixed-size output buffer being filled by repeated `iconv()` calls, as in the [`bytestring-encoding`](https://github.com/msakai/bytestring-encoding/issues/15) / GHC case.

### When it was introduced and fixed

Tracing the tags shows the bug entered slightly *after* the switch to Citrus, not at the moment of the switch:

|version|date|state|
|-|-|-|
|libiconv-64|(macOS 13)|GNU libiconv; this source file does not even exist — unaffected|
|libiconv-80.1.1|2023-09 (macOS 14.0)|Citrus, but **still one character at a time** (no batching, no bug)|
|**libiconv-86**|2023-11|**batching (`cstombn`) introduced — bug introduced at the same time**|
|libiconv-92 … 107|… 2024-09 (macOS 15)|bug present|
|**libiconv-113**|2025-10 (macOS 26)|**fixed** (the `if (ret == 0)` guard removed)|

So the regression was introduced by the batching optimization in `libiconv-86` (a macOS 14.x point release), and the `macos-14` GitHub Actions runner ships a `libiconv` ≥ 86, hence the FAIL.

## Links

* [#24161: iconv regression in recent macOS distributions · Issues · Glasgow Haskell Compiler / GHC · GitLab](https://gitlab.haskell.org/ghc/ghc/-/issues/24161)
* [Problems with iconv on macOS - The R Blog](https://blog.r-project.org/2024/12/11/problems-with-iconv-on-macos/)
* https://github.com/Homebrew/homebrew-core/pull/142490#issuecomment-1752444627

