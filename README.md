ドパガキIME（Mozc のフォーク）
==============================

打つたびにコンボが増え、ランク（B → A → S → SS → SSS）が上がるほど画面が派手になる、Windows 用の日本語入力です。
変換のしくみは [Mozc](https://github.com/google/mozc) そのままで、演出と効果音を足しています。

- 1打ごとに火花が飛び、効果音が鳴ります。ランクが上がるとバナーと花火、BackSpace で「BREAK」。
- 画面のふちが光り、候補ウィンドウが虹色の枠と「x42」のコンボ表示つきになります。3秒打たないと静かに消えます。
- 5コンボごとに、画面のどこかに英語の褒め言葉（「Nice!」「GODLIKE!!」など）が12種類の動きと5種類の色で出ます。200・300…コンボごとには、画面の真ん中に特大の「200 COMBO!!」が出ます。
- タスクバーの「あ」を右クリックすると、効果音（7種類）・演出の ON/OFF・褒め言葉の ON/OFF を切り替えられます。設定は再起動しても残ります。
- 数えるのは打った「回数」だけです。打った文字の内容は保存も送信もしません。パスワード欄では数えません。

### 入れ方

1. このリポジトリの [Actions](https://github.com/koutatauko1-lab/mozc/actions/workflows/windows.yaml) で、いちばん新しい成功したビルドを開き、`Mozc64_x64.msi` をダウンロードします（GitHub へのログインが必要です）。
2. ダウンロードした zip を展開し、中の `Mozc64.msi` を開きます。管理者の確認画面が出たら「はい」を押します。
3. インストールが終わったら PC を再起動します。
4. Win＋スペースで「ドパガキIME」を選びます。

消すときは、Windows の設定 →「アプリ」→「ドパガキIME」→「アンインストール」です。

### このフォークについて

- 仕様・設計・進捗は [DOPAGAKI.md](DOPAGAKI.md)、演出と効果音の見本は [docs/dopagaki/preview.html](docs/dopagaki/preview.html) にあります。
- ビルドは GitHub Actions（`.github/workflows/windows.yaml`）で行います。
- ライセンスは Mozc と同じ BSD 3-Clause です（[LICENSE](LICENSE)）。Mozc と Google はこのフォークとは関係ありません。

以下は、もとの Mozc の README です。

---

[Mozc - a Japanese Input Method Editor designed for multi-platform](https://github.com/google/mozc)
===================================

Copyright 2010-2026 Google LLC

Mozc is a Japanese Input Method Editor (IME) designed for multi-platform such as
Android OS, Apple macOS, Chromium OS, GNU/Linux and Microsoft Windows.  This
OpenSource project originates from
[Google Japanese Input](http://www.google.com/intl/ja/ime/).

Mozc is not an officially supported Google product.

Build Status
------------

| Linux | Windows | macOS | Android lib |
|:-----:|:-------:|:-----:|:-----------:|
| [![Linux](https://github.com/google/mozc/actions/workflows/linux.yaml/badge.svg)](https://github.com/google/mozc/actions/workflows/linux.yaml) | [![Windows](https://github.com/google/mozc/actions/workflows/windows.yaml/badge.svg)](https://github.com/google/mozc/actions/workflows/windows.yaml) | [![macOS](https://github.com/google/mozc/actions/workflows/macos.yaml/badge.svg)](https://github.com/google/mozc/actions/workflows/macos.yaml) | [![Android lib](https://github.com/google/mozc/actions/workflows/android.yaml/badge.svg)](https://github.com/google/mozc/actions/workflows/android.yaml) |


What's Mozc?
------------
For historical reasons, the project name *Mozc* has two different meanings:

1. Internal code name of Google Japanese Input that is still commonly used
   inside Google.
2. Project name to release a subset of Google Japanese Input in the form of
   source code under OSS license without any warranty nor user support.

In this repository, *Mozc* means the second definition unless otherwise noted.

Detailed differences between Google Japanese Input and Mozc are described in [About Branding](docs/about_branding.md).

For policies on vocabulary and conversion results, see
[Vocabulary Policy](VOCABULARY_POLICY.md).

Build Instructions
------------------

* [How to build Mozc for Android](docs/build_mozc_for_android.md): for Android library (`libmozc.so`)
* [How to build Mozc for Linux](docs/build_mozc_for_linux.md): for Linux desktop
* [How to build Mozc for macOS](docs/build_mozc_in_osx.md): for macOS build
* [How to build Mozc for Windows](docs/build_mozc_in_windows.md): for Windows

Release Plan
------------

tl;dr. **There is no stable version.**

As described in [About Branding](docs/about_branding.md) page, Google does
not promise any official QA for OSS Mozc project.  Because of this,
Mozc does not have a concept of *Stable Release*.  Instead we change version
number every time when we introduce non-trivial change.  If you are
interested in packaging Mozc source code, or developing your own products
based on Mozc, feel free to pick up any version.  They should be equally
stable (or equally unstable) in terms of no official QA process.

[Release History](docs/release_history.md) page may have additional
information and useful links about recent changes.

License
-------

All Mozc code written by Google is released under
[The BSD 3-Clause License](http://opensource.org/licenses/BSD-3-Clause).
For third party code under [src/third_party](src/third_party) directory,
see each sub directory to find the copyright notice.  Note also that
outside [src/third_party](src/third_party) following directories contain
third party code.

### [src/data/dictionary_oss/](src/data/dictionary_oss)
Mixed.
See [src/data/dictionary_oss/README.txt](src/data/dictionary_oss/README.txt)

### [src/data/test/dictionary/](src/data/test/dictionary)
The same as [src/data/dictionary_oss/](src/data/dictionary_oss).
See [src/data/dictionary_oss/README.txt](src/data/dictionary_oss/README.txt)

### [src/data/test/stress_test/](src/data/test/stress_test)
Public Domain.  See the comment in
[src/data/test/stress_test/sentences.txt](src/data/test/stress_test/sentences.txt)
