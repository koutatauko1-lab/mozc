# ドパガキIME 改造指示書（Claude Code 向け）

このリポジトリ（`koutatauko1-lab/mozc`、google/mozc のフォーク）を改造して、
**打つたびにコンボが増え、ランクが上がるほど画面が派手になる日本語入力「ドパガキIME」**を作る。
この文書は、作業をする Claude Code のための指示書であり、仕様書であり、進捗ノートでもある。

- 作業を始める前に、この文書を最後まで読むこと。
- 演出の数値と音の作り方の「正解」は `docs/dopagaki/preview.html`（ブラウザで動くプレビュー）にある。迷ったらそれを読むこと。
- 各フェーズが終わったら、末尾の「進捗ノート」を更新すること。

---

## 0. オーナーについて・進め方のルール

オーナーは**こーた**。コードは書かない。確認は「インストールして実際に打ってみる」で行う。
説明はやさしい日本語で、専門用語は最小限に。

**必ず守ること**

1. **勝手に先へ進まない。** フェーズ（第6章）を1つ終えたら、何ができたか・どう確かめればいいかを報告して、こーたの OK を待つ。トークン消費を気にしているので、頼まれていない作業はしない。
2. **決まっていないことは、実装前に聞く。** 第3章の「仮決め」は、そのフェーズに入る前に確認する。
3. **文字入力を絶対に壊さない。** 演出まわりで何が失敗しても、キー入力・変換・確定の動きは今までどおりでなければならない（詳しくは第8章）。
4. **ビルドは GitHub Actions で行う**（第7章）。こーたのPCに巨大な開発環境は入れない。1回のビルドに時間がかかるので、push する前に自分でコードをよく見直し、コンパイルエラーを出さないこと。
5. 変更は最小限に。既存のファイルを直すときは、ドパガキ用の処理を新しいファイルに分けて、既存側は呼び出しを足す程度にとどめる（あとで本家の更新を取り込みやすくするため）。

---

## 1. 前提知識：Mozc（Windows版）の仕組み

```
[文字を打っている各アプリのプロセス]                 [mozc_renderer.exe]（1つだけ動く）
  mozc_tip64.dll / mozc_tip32.dll   ── IPC ──▶   Win32Server（受信）
   ・キー入力を受け取る（TSF）                       └ WindowManager
   ・変換結果から「描画命令」を作る                       ├ CandidateWindow（変換候補ウィンドウ）
     （RendererCommand）                                  ├ IndicatorWindow（「あ」表示）
                                                          └ InfolistWindow（用例）
                         ── IPC ──▶   [mozc_server.exe]（変換エンジン・設定の保存）
```

- **TIP**（`win32/tip/`）：各アプリの中に読み込まれるDLL。キー入力は `TipTextServiceImpl::OnTestKeyDown` などに来る（`win32/tip/tip_text_service.cc`）。32bit版と64bit版の両方がビルドされる。
- **レンダラー**（`renderer/win32/`）：候補ウィンドウなどを描く別プロセス。描画命令は `protocol/renderer_command.proto` の `RendererCommand`。
- TIP → レンダラーの送信は `renderer/win32/win32_renderer_client.cc` の `SenderThread`。
- レンダラーの受信は `renderer/win32/win32_server.cc` の `Win32Server::AsyncExecCommand` → UIスレッドで `ExecCommand`。
- 候補ウィンドウの描画は `renderer/win32/candidate_window.cc`（GDI＋DirectWrite、`DoPaint` 以下）。
- 見た目の色は `renderer/renderer_style.textproto`（ライト）と `renderer_style_dark.textproto`（ダーク）。Windows版は今はライト固定（`renderer/win32/win32_dpi_util.cc` の `GetScaledRendererStyle`）。
- タスクバーの「あ」アイコンの右クリックメニューは `win32/tip/tip_lang_bar.cc` の `input_mode_button_for_win8_`（`GUID_LBI_INPUTMODE`）。
- 設定は `protocol/config.proto` の `Config`。保存は mozc_server が行い、TIP からは `client::ClientFactory::NewClient()` で作ったクライアントの `GetConfig` / `SetConfig` で読み書きできる。レンダラーは `config::ConfigHandler::GetSharedConfig()` で読める。
- **リリースビルドでは `LOG()` が消える**（`.bazelrc` の `ABSL_MIN_LOG_LEVEL=100`）。動作確認用のログは `OutputDebugStringW(L"[dopagaki] ...")` で出し、Sysinternals の DebugView で見る。

### 1.1 一番大事な落とし穴：描画命令は「最新の1件だけ」しか届かない

TIP とレンダラーの間には、**古い命令を新しい命令で上書きする仕組みが2か所**ある。

1. TIP側：`SenderThread::UpdateCommand` は未送信の命令を新しい命令で上書きする。
2. レンダラー側：`Win32Server::AsyncExecCommand` も、UIスレッドが処理する前に次が来たら上書きする（「プロトコルは状態を持たないので上書きしてよい」という設計）。

そのため「キーが押されたよ」という1回ごとのイベントを送ると、**速く打つと取りこぼす**。しかも、ドパガキ用の命令が候補ウィンドウ更新の命令を上書きすると、**候補ウィンドウが表示されなくなる**。これを避けるため、次の設計にする（第4章で詳しく）。

- TIP は「これまでに数えた打鍵の合計」などの**累積カウンタ**を送る。レンダラーは前回との差分でコンボを増やす。上書きされても合計が届けば数がずれない。
- ドパガキ用の情報は、既存の描画命令とは**別の置き場所**で受け渡す（どちらの上書きにも巻き込まれないようにする）。

---

## 2. 決定済みの仕様（こーた承認済み）

### 2.1 コンボとランク

| 項目 | 仕様 |
|---|---|
| コンボの増え方 | キーを1回押すごとに +1 |
| 手が止まったとき | **コンボは切れない**（BackSpace だけが敵） |
| BackSpace | **容赦なくコンボ0**、同時に「BREAK」演出 |
| ランク | コンボ数で決まる。B：0〜9 ／ A：10〜29 ／ S：30〜49 ／ SS：50〜99 ／ SSS：100〜 |
| コンボの範囲 | パソコン全体で1つ（アプリを切り替えても続く） |

### 2.2 ランク別の演出（数値はプレビューと同じ）

長さの単位は「96dpi のときのピクセル」。実際の画面ではモニターの DPI に合わせて拡大する（`GetDpiForMonitor`）。
「/フレーム」の値は 60fps 前提。実装では経過時間に合わせて換算すること。

| ランク | 色 | 揺れの強さ | 1打で出る粒 | 候補枠の光 | ふちの線の太さ | ふちのぼかし | ふちの濃さ | ふちの火花（/フレーム） |
|---|---|---|---|---|---|---|---|---|
| B | `#4DA3FF`（青） | 0 | 4 | 8 | なし | なし | 0 | 0 |
| A | `#3BE37A`（緑） | 1 | 10 | 14 | 3 | 14 | 0.45 | 0 |
| S | `#FFD23F`（金） | 3 | 18 | 22 | 6 | 22 | 0.70 | 0.06 |
| SS | `#FF4FD8`（ピンク） | 6 | 30 | 32 | 9（脈打つ） | 30 | 0.85 | 0.6 |
| SSS | 虹（`hsl(色相, 100%, 62%)`、色相は毎フレーム +3°） | 10 | 50 | 44 | 13（脈打つ・虹が回る） | 42 | 1.0 | 2.2 |

**打つたびの粒（パーティクル）**
- 出どころ：候補ウィンドウ右上のコンボ表示の位置（候補ウィンドウが無いときは第5.6節の順で決める）。
- 速さ：`(4 + ランク番号×1.6) × (0.4〜1.4 のランダム)` px/フレーム（ランク番号は B=0, A=1, S=2, SS=3, SSS=4）。
- 大きさ：`(2.5 + ランク番号×0.6) × (0.5〜1.5)` px の丸。
- 動き：最初に上向き 1.5 を足す。重力 +0.18/フレーム、横の減速 ×0.985/フレーム。
- 寿命：1 から毎フレーム 0.012〜0.032 ずつ減り、0 で消える。濃さ＝寿命。色は加算合成（重なるほど明るい）。
- 色：ランク色（SSS は現在の色相から ±120° のランダム）。
- SS は 20%、SSS は 50% の確率で、1打ごとに画面のランダムな位置（横は全体、縦は上60%）でも花火（同じ粒数・速さ7・大きさ3.5・上向き2）。

**ランクアップ（ランクが上がった瞬間）**
- 画面中央に「`S RANK!!`」のような大きな文字（SSS なら「`SSS RANK!!`」）。
  - 大きさ：モニター幅の18%（最小64、最大220px@96dpi）。色はランク色、外側に強い光（ぼかし30と80）＋下に黒い影。
  - 動き（1000ms）：拡大率と濃さが 0%:0.3/透明 → 18%:1.15/不透明 → 70%:1.0/不透明 → 100%:1.6/透明。イージングは `cubic-bezier(.2,.8,.2,1)`。
- 画面のランダムな位置で `2 + ランク番号×2` 回の花火（各30粒・速さ8・大きさ4）。
- 揺れ：`そのランクの揺れ×2.2 + 4`。
- 効果音は「ランクアップ音」（2.5節）。

**BREAK（BackSpace を押した瞬間）**
- コンボが0のときに押した場合は、BREAK音だけ鳴らす（演出はなし）。
- 画面全体が赤く光る：`#FF2A3D`、濃さ 0.75 → 0 を 450ms（ease-out）。
- 画面中央に「`BREAK`」を全面にバーンと出す（色 `#FF2A3D`、強い赤い光）。
  - 動き（1100ms、全体を -4° 傾ける）：0%:拡大2.4/透明 → 12%:1.0/不透明 → 70%:1.04/不透明 → 100%:0.9・30px 下へ・-6°/透明。
- 赤い破片：コンボ表示の位置から60個（`#FF2A3D`、速さ10、大きさ5）、画面中央から80個（`#FF6A3D`、速さ14、大きさ6）。破片は細長い四角で回転しながら飛ぶ。
- 揺れ：14。
- 効果音は「BREAK音」。

**揺れ**
- 毎フレーム、上下左右に `±揺れの強さ` px のランダムなずれ。強さは毎フレーム ×0.86 で弱まり、0.2 未満で止まる。
- Windows の「アニメーション効果」がオフ（`SPI_GETCLIENTAREAANIMATION` が FALSE）のときは揺らさず、火花の量を 30% に減らす。

### 2.3 画面のふち（ベゼル沿い）の光と火花

- **文字を打っているモニターだけ**、画面の一番外側に沿って光の帯を描く。モニターが2枚あっても、もう片方は光らせない。
- ランク別の変化は上の表のとおり：
  - B：光らない
  - A：うっすらランク色に光る
  - S：線が太くなり、たまに火花がパチッ
  - SS：光が脈打ち（太さ×(1+0.35·sin(t×2))）、火花が常に散る
  - SSS：虹色の光がふちに沿って回り（回転するグラデーション）、脈打ち（sin(t×3)）、四辺から火花が噴き出す
  - t は毎フレーム +0.05。
- 描き方（プレビューの `drawEdge` と同じ）：外側の線（太さ×2、濃さ＝表の値、ぼかし＝表の値×脈）＋内側の線（太さ×0.8、内側へ太さ×0.6 ずらす、濃さ＋0.2）。
- 火花：ふちのランダムな1点から内側へ飛ぶ細長い破片。速さ `4 + ランダム×(4 + ランク番号×2)`、横方向に ±0.7 倍ぶれる、大きさ1.5〜3.5、寿命の減り 0.025〜0.055/フレーム、色はランク色（SSS は虹）。

### 2.4 変換候補ウィンドウ（参考画像どおりのドパガキ仕様）

- 背景は暗い色（`#1D1B2A`）、行の区切り線 `#2A2838`、番号の文字 `#7C7A92`。ベースは `renderer_style_dark.textproto`。
- 角を丸くする（Windows 11 なら `DwmSetWindowAttribute(DWMWA_WINDOW_CORNER_PREFERENCE, DWMWCP_ROUND)`。Windows 10 は四角のままでよい）。
- **選択中の行**：左から右へのグラデーション。左端＝ランク色、右端＝ランク色40%＋`#7B2CFF` 60% を混ぜた色。文字が読みにくければ、グラデーションを少し暗くする（明るさ70%程度）。
- **ウィンドウの外枠**：太さ3pxの枠をランク色で光らせる（外側に「候補枠の光」の値でぼかした光）。SSS は虹色の枠がぐるぐる回る。
- **コンボ表示**：ウィンドウの右上の角に「`x42`」（太字・斜体・22px・ランク色・光）。少しはみ出す位置（上に14px、右に10px）。1打ごとに 1.5倍 → 1倍 に縮む（160ms）。

### 2.5 効果音（6種類＋ミュート）

音声ファイルは使わず、プレビューと同じ方法で**その場で波形を作って鳴らす**（`docs/dopagaki/preview.html` の `SOUNDS`）。速く打っても音が途切れないよう、複数の音を同時に鳴らせるようにする（XAudio2 推奨）。

部品（プレビューの関数と同じ動き）：
- `tone(波形, 開始周波数f0, 終了周波数f1, 長さ, 音量, 遅れ)`：周波数を f0→f1 へ指数的に変化。音量は開始値から 0.0001 へ指数的に減衰。
- `noise(長さ, 音量, ハイパス周波数, 遅れ)`：ホワイトノイズを直線的に減衰させ、ハイパスフィルタ（双二次）を通す。
- r＝ランク番号（0〜4）、rand＝0〜1 の乱数。時間の単位は秒。

| 種類 | 1打ごとの音 | ランクアップ音 | BREAK音 |
|---|---|---|---|
| ピコピコ（8bit） | square, 660+r·120+rand·80 → 880+r·160, 0.06, 0.06 | square 523/659/784/1046Hz を0.07秒おきに（各0.09, 0.07） | square 440→55, 0.5, 0.09 |
| タイプライター | noise 0.03, 0.35, hp2500 | sine 1760, 0.6, 0.12 ＋ sine 2637, 0.5, 0.05 | noise 0.25, 0.5, hp300 ＋ triangle 180→60, 0.35, 0.12 |
| 打撃（格ゲー） | sine 160+r·20 → 50, 0.09, 0.25 ＋ noise 0.04, 0.18, hp1800 | sine 90→40, 0.4, 0.35 ＋ noise 0.3, 0.3, hp600 | noise 0.5, 0.55, hp200 ＋ sawtooth 220→40, 0.6, 0.12 |
| 和太鼓 | sine 140+r·15 → 70, 0.16, 0.35 | sine 110→55, 0.3, 0.45 を 0/0.14/0.28秒に | sine 90→35, 0.9, 0.5 ＋ noise 0.15, 0.2, hp900（0.02秒遅れ） |
| キラキラ | sine 1500+rand·900+r·200 → 2400+r·200, 0.09, 0.05 | sine 1046/1318/1568/2093/2637Hz を0.05秒おきに（各0.25, 0.06） | triangle 1568/1174/880/587Hz（各 f→f×0.7, 0.2, 0.07）を0.08秒おきに |
| メカ（レーザー） | sawtooth 1400+r·200 → 300, 0.07, **0.08**（プレビューは 0.04。小さすぎたので、こーたの指示で2倍にした） | sawtooth 200→2400, 0.35, 0.06 ＋ square 300→3000, 0.35, 0.03 | sawtooth 1800→40, 0.6, 0.08 ＋ noise 0.4, 0.3, hp500 |
| ミュート | 鳴らさない | 鳴らさない | 鳴らさない |

- ランクが上がった打鍵では「1打ごとの音」の代わりに「ランクアップ音」を鳴らす。
- 初期値は「ピコピコ」。音量は Windows の音量に従う（独自の音量設定は作らない）。

### 2.6 設定の切り替え

- タスクバーの「あ」アイコンを**右クリックしたメニュー**に、区切り線のあとドパガキ用の項目を足す。
  - 効果音：ピコピコ（8bit）／タイプライター／打撃（格ゲー）／和太鼓／キラキラ／メカ（レーザー）／ミュート（どれか1つにチェック）
- 設定は保存され、PCを再起動しても残る。
- Mozc から Google 日本語入力などに切り替えたら、演出もまとめて止まる（Mozc のときだけ動く）。

### 2.7 画面全体の演出の描き方

- 画面全体を覆う**透明なウィンドウ**（以下「オーバーレイ」）を、文字を打っているモニターに重ねて、そこに粒・バナー・赤い光・ふちの光を描く。
- オーバーレイは**クリックやキー入力を一切奪わない**（下のアプリがそのまま操作できる）。
- 何も描くものが無いときは、オーバーレイを隠して描画を止める（全画面ゲームや動画の邪魔をしない・電気を食わない）。

---

## 3. 仮決め（そのフェーズに入る前に、こーたに確認すること）

プレビューでは決めきれなかった細かい点。下は「こうするつもり」の案。**実装前に確認し、決まったらこの表を更新すること。**

| # | 内容 | 案 | 関係するフェーズ |
|---|---|---|---|
| K1 | キーを押しっぱなしにしたときの連打（オートリピート） | **決定**：数えない。BackSpace 長押しでも BREAK は1回 | 1 |
| K2 | パスワード入力欄 | **決定**：数えない・演出しない | 1 |
| K3 | 数えないキー | **決定**：Shift / Ctrl / Alt / Windows キー / アプリキー / CapsLock / Tab / Esc / 半角全角 / NumLock / ScrollLock（プレビューと同じ＋IME切替キー）。変換・無変換キーは数える | 1 |
| K4 | しばらく打たないとき | **決定**：3秒入力が無ければ、ふちの光・火花・コンボ表示は1秒かけてフェードアウトして消える。コンボの数とランクは保持し、次の1打ですぐ復活。出ている途中の演出（バナー・粒）は最後まで出す | 3 |
| K5 | 候補ウィンドウが出ていないとき（英字の直接入力など）のコンボ表示 | **決定**：入力位置（カーソル）の近くに「`29 COMBO A`」を出し、2秒で消える（元ネタの動画と同じ雰囲気）。カーソル位置が分からないアプリでは打っているウィンドウの端あたり。粒もここから出す | 2 |
| K6 | メニューに「ドパガキ演出 ON/OFF」も付けるか | **決定**：付ける（会議中などに一時的に止められるように） | 6 |
| K7 | IMEの表示名を「ドパガキIME」に変えるか | **決定**：変える。ただし画面に出る名前の2か所だけ：①IME の一覧（Win＋スペース、Windows の設定の「キーボード」一覧）②Windows の設定の「アプリ」一覧。フォルダ名・ファイル名・内部の登録ID（GUID）・設定の保存場所・`kProductNameInEnglish`、プロパティ画面などの中の「Mozc」は変えない（入力を壊さない・設定を消さないため。Mozc の作者表示はライセンス上も残す）。英語の Windows では「Dopagaki IME」 | 7 |

**制約（こーたに説明済みにしておくこと）**：画面の揺れは、自分で描いている演出（粒・バナー・ふち・コンボ表示）と候補ウィンドウにだけかかる。他のアプリの画面そのものは揺らせない（他アプリのウィンドウを動かすのは危険なのでやらない）。

---

## 4. 全体設計

```
[アプリ内 TIP]                                         [mozc_renderer.exe]
 TipDopagaki（新規）                                    Win32Server
  ・OnTestKeyDown/OnKeyDown で打鍵を数える               ├ ドパガキ専用の受け取り口（新規）
  ・累積カウンタを持つ                                    │    └ UIスレッドへ通知
  ・設定変更をメニューから受ける                          ├ DopagakiState（新規）：差分→コンボ・ランク・イベント
        │                                                ├ WindowManager（既存）：候補ウィンドウの位置を共有
        ▼                                                ├ CandidateWindow（既存を改造）：ダーク・角丸・グラデ
 Win32RendererClient::OnDopagakiEvent（新規）            ├ DopagakiOverlay（新規）：全画面の透明ウィンドウ
  ・既存の命令とは別枠で保持して送る                       └ 効果音の依頼（TIP のコールバックウィンドウへ PostMessage）
 TipDopagakiSound（新規）：XAudio2 で効果音（4.8）
```

### 4.1 送る情報（`protocol/renderer_command.proto` に追加）

```proto
// --- Dopagaki (fork only) ---
message DopagakiEvent {
  // TIP の DLL が読み込まれるたびに作る乱数。プロセスの区別に使う。
  optional fixed64 session_id = 1;
  // このセッションで数えた打鍵の累計（BackSpace は含めない）。
  optional uint64 key_count = 2;
  // このセッションで押された BackSpace の累計。
  optional uint64 break_count = 3;
  // 最後の BackSpace 以降に数えた打鍵の数。
  optional uint64 keys_since_break = 4;
  // 打っているアプリのウィンドウ（モニター判定用）。WinUtil::EncodeWindowHandle の値。
  optional uint32 target_window_handle = 5;
  // こーたがメニューで設定を変えたときだけ入れる。
  optional mozc.config.Config.DopagakiConfig settings = 6;
}

// RendererCommand の中に追加。本家と番号がぶつからないよう大きい番号にする。
optional DopagakiEvent dopagaki = 1000;
```

設定（`protocol/config.proto` の `Config` の中に追加。こちらも大きい番号）：

```proto
message DopagakiConfig {
  enum SoundSet {
    PICO = 0; TYPEWRITER = 1; HIT = 2; TAIKO = 3; SPARKLE = 4; MECHA = 5; MUTE = 6;
  }
  optional bool enabled = 1 [default = true];
  optional SoundSet sound_set = 2 [default = PICO];
}
optional DopagakiConfig dopagaki_config = 7000;
```

- `renderer_command.proto` は今 `commands.proto` しか import していない。proto の import は孫まで自動では見えないので、`import "protocol/config.proto";` を明示的に足し、`protocol/BUILD.bazel` の `renderer_proto` の `deps` に `":config_proto"` を足すこと。
- うまくいかない場合は、同じ形の message を `renderer_command.proto` 側に定義してもよい。

### 4.2 TIP 側：打鍵を数える（新規 `win32/tip/tip_dopagaki.{h,cc}`）

- `TipTextServiceImpl::OnTestKeyDown` と `OnKeyDown`（`tip_text_service.cc`）の**両方**の先頭で `TipDopagaki::OnTestKeyDown` / `TipDopagaki::OnKeyDown` を呼び、それから既存の処理に進む。**`eaten` の結果には一切影響させない。**
  - **フェーズ1で判明**：メモ帳（Windows 11）と Brave では `OnTestKeyDown` が一度も呼ばれず、`OnKeyDown` だけが呼ばれた。最初は `OnTestKeyDown` だけで数えていたため、まったく数えられなかった。アプリによっては両方呼ばれる可能性があるので、両方で数えて重複を除く。
- 同じキーを二重に数えない工夫：`(wparam, lparam, スレッド)` が直近8件のどれかと同じで、「50ms 以内」または「`::GetMessageTime()` が同じで1秒以内」なら同じ打鍵とみなす（`tip_dopagaki.cc` の `IsCountedKeystrokeLocked`）。メモ帳では1打が1回だけ数えられることをログで確認済み。
- オートリピートは `lparam` の bit30（直前にキーが押されていた）で判定して数えない（K1）。
- パスワード欄など：`TipStatus::IsDisabledContext(context)` か、入力欄の InputScope に `IS_PASSWORD` があれば数えない（K2）。InputScope は `TipInputModeManager::input_scope()`（getter を1行追加）から読む。
- 数えないキーは K3 のとおり。`VK_BACK` は「BREAK」として `break_count` を +1、`keys_since_break` を 0 に。
- カウンタはプロセス全体で1組（SRWLOCK で守る。持つのは数行の計算の間だけ）。数えたら `Win32RendererClient::OnDopagakiEvent(...)` を呼ぶ。
- `target_window_handle` は、フォーカスのある `ITfContext` のビューから取れる HWND（`FillMozcContextCommon` と同じ方法）か、取れなければ `::GetForegroundWindow()`。
- **キー処理の中で重い処理や待ち合わせをしない。** 値をセットして送信スレッドに知らせるだけにする。

### 4.3 TIP 側：送信（`renderer/win32/win32_renderer_client.{h,cc}`）

- 新しい静的関数 `Win32RendererClient::OnDopagakiEvent(const commands::DopagakiEvent&)` を追加。
- `SenderThread` に、既存の `renderer_command_` とは**別に** `pending_dopagaki_`（最新の DopagakiEvent）と「未送信フラグ」を持たせる。
  - 既存の UI 命令が未送信なら、その命令に `dopagaki` を付けて一緒に送る。
  - UI 命令が無く、ドパガキだけが未送信なら、`type = NOOP` の命令に `dopagaki` だけ入れて送る。
  - どちらも、ドパガキ側は常に最新の累計で上書きしてよい（累計なので失われない）。
- `CanIgnoreRequest` は `visible == false` の命令を捨てることがあるので、ドパガキの経路はこれを通さず `EnsureUIThreadInitialized` を呼ぶ。
- `renderer/renderer_client.cc` の `RendererClient::ExecCommand` は、命令の `visible` で `is_window_visible_` を書き換える。**ドパガキだけの命令（NOOP）では `is_window_visible_` を変えない**ようにする。
- レンダラーが起動していないときに NOOP を送ると、既存の仕組みでレンダラーが起動される。これはそのままでよい（直接入力中でも演出が出るように）。

### 4.4 レンダラー側：受け取り（`renderer/win32/win32_server.{h,cc}`）

- `Win32Server::AsyncExecCommand` で受け取った文字列を `RendererCommand` として読み、
  - `dopagaki` があれば、**別の置き場所**（`absl::Mutex` で守った `latest_dopagaki_`）に保存して、UIスレッドに知らせる（2つ目のイベントハンドルを作り、`StartMessageLoop` の `MsgWaitForMultipleObjects` で両方を待つ）。
  - その命令が `NOOP` でドパガキだけなら、既存の `message_` には**触らない**（候補ウィンドウ更新の命令を上書きしないため）。
  - それ以外（UPDATE など）は今までどおり `message_` に入れる。
- UIスレッドでドパガキの通知を受けたら `WindowManager::OnDopagakiEvent(event)` を呼ぶ。
- `ExecCommand` の `UPDATE visible=false → HideAllWindows` でオーバーレイまで消さないこと（オーバーレイの表示はドパガキ側が管理する）。

### 4.5 コンボの計算（新規 `renderer/win32/dopagaki_state.{h,cc}`。Windows API を使わない純粋なロジック）

- `session_id` ごとに前回の `key_count` と `break_count` を覚えておく。
- 届いた値と前回の差で処理する：
  - `break_count` が増えていたら：BREAK イベントを出し、コンボ＝`keys_since_break`（BREAK のあとに打った分）にする。
  - そうでなければ：コンボ += `key_count` の増分。
- 初めて見る `session_id` の場合：BREAK は起こさず、`keys_since_break` を増分とみなすが、**最大5まで**に制限する（レンダラーが再起動した直後に大量加算されるのを防ぐ）。
- 古いセッションは、しばらく来なければ（例：10分）忘れてよい。
- ランクの境目を越えたら「ランクアップ」イベント（一度に2段階越えたら一番上のランクだけ）。
- 結果は「打鍵イベント（何打ぶんか）」「ランクアップ」「BREAK」の列として返し、演出と音はそれを見て動く。
- このクラスには**単体テスト**を書く（`mozc_cc_test`。例：普通に加算、BREAK 後の値、上書きで2打まとめて届いたとき、ランクアップ、初見セッションの上限）。

### 4.6 オーバーレイ（新規 `renderer/win32/dopagaki_overlay.{h,cc}`、`dopagaki_effects.{h,cc}`）

- `dopagaki_effects`：粒・破片・バナー・赤い光・揺れ・ふちの状態を持ち、「経過時間」で更新する**純粋なロジック**。数値は第2章どおり。
- `dopagaki_overlay`：画面を覆う透明ウィンドウと描画。
  - 推奨方式：**DirectComposition ＋ Direct3D11 ＋ Direct2D**（`CreateSwapChainForComposition`、プリマルチプライドアルファ）。GPU で描くので全画面60fpsでも軽い。
  - ウィンドウの拡張スタイル：`WS_EX_NOREDIRECTIONBITMAP | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE`（クリックをすり抜けさせ、タスクバーや Alt+Tab に出さず、フォーカスを奪わない）。表示は `SW_SHOWNOACTIVATE`。
  - **クリックがすり抜けるか必ず実機で確認すること。** すり抜けない場合は、`UpdateLayeredWindow`（インジケーターウィンドウ `indicator_window.cc` と同じ方式）に切り替える。その場合は全画面ではなく「ふちの4本の帯」「バナー用」「粒用」に分けて、描く面積を減らす。
  - 位置と大きさ：打っているモニター（`MonitorFromWindow(target_window_handle)`）の `rcMonitor`。モニターが変わったら移動。DPI はそのモニターのもの。
  - 重なり順：候補ウィンドウより上（`WindowManager` が候補ウィンドウを前面に出した直後に、オーバーレイを `SetWindowPos(HWND_TOPMOST, ..., SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE)` で上に戻す）。
  - アニメーションは UIスレッドのタイマー（約16ms）で回す。UIスレッドを長く止めない（`Present` は待たない設定にする）。
  - 何も描くものが無くなったら、タイマーを止めてウィンドウを隠す。
  - 文字（バナー・コンボ表示）は DirectWrite。フォントは「BREAK」「RANK!!」が `Impact`（無ければ `Arial Black`）、`x42` やコンボ数が `Bahnschrift`（太字・斜体）。
  - 初期化（D3D・DComp など）に失敗したら、オーバーレイ機能だけを止める。**レンダラーを落とさない。**

### 4.7 候補ウィンドウの改造（`renderer/win32/candidate_window.{h,cc}`、`win32_dpi_util.cc`、`window_manager.cc`）

- `GetScaledRendererStyle` をダークスタイル（`RendererStyleHandler::GetRendererStyle(ColorTheme::kDark)`）に変え、背景などの色を 2.4節の値に合わせる。
- `CandidateWindow::DrawSelectedRect` を、`GradientFill`（横方向）でのグラデーションに変える。色は今のランク（SSS は今の色相）。
- `CandidateWindow` に「今のランクと色相を受け取る」関数を足す。SSS で候補ウィンドウが表示中は、色相の変化に合わせて 30fps 程度で再描画する。
- 角丸は `OnCreate` などで `DwmSetWindowAttribute` を呼ぶ。
- 外枠の光・虹の枠・`x42` は**オーバーレイ側**で描く（ウィンドウの外にはみ出すため）。そのために `WindowManager::UpdateLayout` で決めた候補ウィンドウの位置（`main_window_rect`）と表示中かどうかを覚えておき、オーバーレイに渡す。

### 4.8 効果音（`renderer/win32/dopagaki_sound_synth.{h,cc}` ＋ `win32/tip/tip_dopagaki_sound.{h,cc}`）

- **フェーズ5で判明：レンダラーでは音を鳴らせない。** レンダラーは Mozc のサンドボックス（Low 整合性・制限トークン）で動いていて、`CreateMasteringVoice` が `E_ACCESSDENIED`（0x80070005）になる。サンドボックスはゆるめず、次の形にした（こーた承認済み）：
  1. TIP は `DopagakiEvent.receiver_window_handle` に自分のレンダラー用コールバックウィンドウ（`renderer_callback_window_handle()`）を入れて送る。
  2. レンダラーは鳴らす音（1打・ランクアップ・BREAK）を決め、そのウィンドウに `kMessageReceiverMessageName` のメッセージを `PostMessage` する（WPARAM＝`kDopagakiSoundRequest`、LPARAM＝`EncodeDopagakiSoundRequest(音の種類, 何の音, ランク)`）。このメッセージは候補のマウス選択で使っているもので、TIP は `ChangeMessageFilter` 済みなので Low 整合性からでも届く。
  3. TIP のコールバックウィンドウの処理（`RendererCallbackWidnowProc`）で `TipDopagaki::OnRendererCallback` が受け取り、`TipDopagakiSound::Play` がアプリのプロセス内で鳴らす。
- 合成（`dopagaki_sound_synth`）は Windows API を使わない純粋なロジック。2.5節の表をそのまま `tone` / `noise` の組み合わせで合成（矩形波・のこぎり波は PolyBLEP でエイリアスを減らす。ハイパスは Web Audio と同じ双二次・Q=1dB）。テストあり。
- 再生（`tip_dopagaki_sound`）：XAudio2、モノラル float 48kHz、ソースボイス16個を使い回す。プロセスに1つ、最初の音で作る。XAudio2 にコールバックは登録しない（DLL の中を呼ばせない）。サンプルはプロセスヒープに置く（DLL が外れても無効なメモリを読ませない）。最後のテキストサービスが `Deactivate` されたら止める（DllMain では止めない）。
- 初期化に3回失敗したら、そのプロセスでは音だけ止める。

### 4.9 設定メニュー（TIP）

- `win32/tip/tip_lang_bar_callback.h` の `ItemId` に追加：`kDopagakiToggle = 50`（K6 で付ける場合）、`kDopagakiSoundPico = 51` 〜 `kDopagakiSoundMecha = 56`、`kDopagakiSoundMute = 57`。
- `win32/tip/tip_resource.h` と `tip_resource.rc`（日本語と英語の両方の STRINGTABLE）に文字列を追加。例：「効果音：ピコピコ（8bit）」。
- `win32/tip/tip_lang_bar.cc` の `input_mode_button_for_win8_` 用のメニュー配列の最後に、区切り線とドパガキの項目を追加。
- **注意**：`TipLangBarToggleButton`（`tip_lang_bar_menu.cc`）は、選んだ項目を「入力モード」としてラジオチェックし、アイコンまで切り替える作りになっている。ドパガキの項目でこれが起きると、タスクバーのアイコンが壊れる。そのため：
  - `OnMenuSelect`：ドパガキの ID なら、コールバックだけ呼んで、`menu_selected_` やラジオチェックには触らない。
  - `SelectMenuItem`：ドパガキの項目は無視する。
  - メニューを開くとき：タスクバーの「あ」の右クリックメニューは `TipLangBarButton::OnClick` が毎回作っている（`InitMenu` は使われない）。ここで今の設定を読んで（`TipDopagakiMenu::LoadSettings`）、ドパガキ項目のチェックをその場で付ける。チェックの状態は `menu_data` に保存しない。
- 実装は `win32/tip/tip_dopagaki_menu.{h,cc}`（`TipDopagakiMenu`）にまとめる。
- `TipTextServiceImpl::OnMenuSelect`：ドパガキの ID なら、`ClientFactory::NewClient()` で作ったクライアント（エラー画面は出さない設定）で `GetConfig` → `dopagaki_config` を変更 → `SetConfig`。続けて `settings` だけを入れた `DopagakiEvent`（回数なし）を送って、レンダラーにすぐ反映させる（`TipDopagaki::SendSettings`）。送信待ちの打鍵の回数があれば、それは消さずに `settings` だけ重ねる。
- レンダラーは起動時に `ConfigHandler::GetSharedConfig()` から設定を読み、以後は `settings` 付きの命令で更新する（`WindowManager::ApplyDopagakiSettings`）。
- K6 の OFF のとき：画面の演出（粒・バナー・ふち・コンボ表示・候補ウィンドウの枠と `x42`）はすぐ全部消え、候補ウィンドウはランク B の色に戻る。音も鳴らさない。打鍵の回数は裏で追い続けるが、コンボは 0 のまま。ON に戻すと 0 から数え直す。

### 4.10 ビルド設定

- `bazel/win32/BUILD.bazel` に `mozc_win32_lib` で追加：`d3d11`、`dxgi`、`dcomp`、`dwmapi`、`xaudio2`。
- `renderer/win32/BUILD.bazel` に新しいライブラリ（`dopagaki_state`、`dopagaki_effects`、`dopagaki_overlay`、`dopagaki_sound`）とテストを追加し、`window_manager` / `win32_server` / `candidate_window` の deps に足す。
- `win32/tip/BUILD.bazel` に `tip_dopagaki` を追加し、`tip_text_service` などの deps に足す。

---

## 5. 触るファイルの一覧

| ファイル | 内容 |
|---|---|
| `protocol/renderer_command.proto` | `DopagakiEvent` と `dopagaki = 1000` を追加 |
| `protocol/config.proto` | `DopagakiConfig` と `dopagaki_config = 7000` を追加 |
| `win32/tip/tip_dopagaki.{h,cc}`（新規） | 打鍵カウント・重複除外・送信 |
| `win32/tip/tip_dopagaki_menu.{h,cc}`（新規） | 設定メニューのチェックと保存（フェーズ6） |
| `win32/tip/tip_dopagaki_sound.{h,cc}`（新規） | 効果音を鳴らす（フェーズ5） |
| `win32/tip/tip_text_service.cc` | `OnTestKeyDown` と `OnKeyDown` から呼ぶ／`OnMenuSelect` に設定変更 |
| `win32/tip/tip_lang_bar.cc` / `tip_lang_bar_menu.cc` / `tip_lang_bar_callback.h` | メニュー項目の追加と特別扱い |
| `win32/tip/tip_resource.h` / `tip_resource.rc` | メニューの文字列／IME の表示名「ドパガキIME」（K7） |
| `win32/base/display_name_resource.h` / `tsf_profile.cc` / `tsf_registrar.cc` | IME の表示名の文字列ID（新しい ID 202）と登録名（K7） |
| `win32/installer/installer_oss_64bit.wxs` | 「アプリ」一覧に出る名前（K7） |
| `renderer/win32/win32_renderer_client.{h,cc}` | `OnDopagakiEvent` と別枠送信 |
| `renderer/renderer_client.cc` | NOOP で `is_window_visible_` を変えない |
| `renderer/win32/win32_server.{h,cc}` | ドパガキ専用の受け取り口 |
| `renderer/win32/window_manager.{h,cc}` | ドパガキの司令塔（状態・オーバーレイ・音・候補ウィンドウをつなぐ） |
| `renderer/win32/candidate_window.{h,cc}` | ダーク・角丸・グラデーション |
| `renderer/win32/win32_dpi_util.cc` | ダークスタイル |
| `renderer/win32/dopagaki_*.{h,cc}`（新規） | 状態・演出・オーバーレイ・音 |
| `bazel/win32/BUILD.bazel`、各 `BUILD.bazel` | ライブラリとターゲット |
| `.github/workflows/windows.yaml` | ビルドの高速化（フェーズ0） |

---

## 6. 実装フェーズ

**どのフェーズも、終わったら「できたこと」「こーたが確かめること」を報告して止まる。**
各フェーズは1〜数コミットにまとめ、CI が通ってから報告する。

### フェーズ0：ビルドを速く・楽にする
`.github/workflows/windows.yaml` を次のようにする（付録Aに案）。
- 動かすのは x64 版のインストーラー作成だけ（universal・arm64・既存の全テストのジョブは消す）。
- `workflow_dispatch`（手動実行）を追加。`*.md` と `docs/**` だけの変更ではビルドしない（`paths-ignore`）。
- 速いチェック用のジョブ `quick_check` を追加：Qt を作らずに `//renderer/win32:win32_renderer_main` と `//win32/tip:mozc_tip64` とドパガキの単体テストだけをビルド・実行する。これが通ってから本番の `package` ジョブを動かす（`needs:`）。
- Qt のビルド結果（`src/third_party/qt`）を `actions/cache` で保存し、2回目以降は「Build Qt」を飛ばす（キー：`build_tools/build_qt.py` と `build_tools/update_deps.py` のハッシュ）。
- Bazel のディスクキャッシュ（`--disk_cache`）を `actions/cache` で保存・復元する（キャッシュの上限に注意。大きすぎて保存できない場合はやめてよい）。
- （任意）REVISION を `(100 + github.run_number) × 10` にしてからビルドする。こうすると毎回バージョンが上がり、新しい TIP が古いレンダラー／サーバーを自動で終了させて入れ替える（`renderer_client.cc` のバージョン比較）。各桁は 65535 を超えないこと。
  - **注意（フェーズ0で判明）**：`version.bzl` の `REVISION` を書き換えても効かない（既定の dev チャンネルでは `mozc_version.py` が REVISION を 100 に固定する）。`--action_env=MOZC_VERSION=...` は全部の C++ のキャッシュが無効になるので使わない。CI の中だけで `base/BUILD.bazel` の `mozc_version_txt` に `env = {"MOZC_VERSION": "3.34.6345.1030"}` を足して渡す（`windows.yaml` の「Bump REVISION」）。
- **確認**：Actions のビルドが通り、2回目以降が速くなる。できたインストーラーで普通に入力できる。

### フェーズ1：配線（数が数えられることの確認）
- 4.1〜4.5 を実装。演出はまだ作らず、**動作確認用の小さな仮表示**だけ出す：画面右下に「COMBO 12 / A」（GDI で描く小さなレイヤードウィンドウ。あとで消す）。
- 4.5 の単体テストを書き、`quick_check` で実行する。
- 開始前に K1〜K3 を確認。
- **確認**：メモ帳・ブラウザ・VS Code など複数のアプリで打つと数が増え、アプリを切り替えても続く。BackSpace で0になる。速く打っても数がだいたい合う。候補ウィンドウが今までどおり出る。

### フェーズ2：画面全体の演出
- 4.6 を実装し、仮表示を消す。粒・ランクアップのバナーと花火・BREAK（赤い光＋全面の文字＋破片）・揺れ。K5 のコンボ表示も。
- 開始前に K5 を確認。
- **確認**：`docs/dopagaki/preview.html` の自動デモと見比べて、雰囲気が同じ。クリックが下のアプリにちゃんと届く。

### フェーズ3：画面のふち
- 2.3節を実装。文字を打っているモニターだけ。K4 のフェードアウト。
- 開始前に K4 を確認。
- **確認**：モニター2枚のとき、打っている側だけ光る。ランクごとに変化する。3秒放置で消え、次の1打で戻る。

### フェーズ4：候補ウィンドウ
- 4.7 と 2.4節を実装。
- **確認**：参考画像（虹色の枠・グラデーションの選択行・右上の `x42`）のような見た目になる。文字が読める。

### フェーズ5：効果音
- 4.8 と 2.5節を実装。切り替えメニュー（フェーズ6）ができるまでは、初期値の「ピコピコ」で鳴る。
- **確認**：速く打っても音が途切れない。プレビューの同じ種類の音と聴き比べて近い。

### フェーズ6：設定メニュー
- 4.9 を実装。開始前に K6 を確認。
- **確認**：「あ」を右クリックして効果音を切り替えられる。チェックが正しく付く。アイコンが壊れない。PCを再起動しても設定が残る。

### フェーズ7：仕上げ
- 開始前に K7 を確認。
- 長時間（30分以上）使ってもメモリやCPUが増え続けない。全画面の動画やゲームの上で、何もしなければオーバーレイが隠れている。
- パスワード欄・高DPIモニター・モニター2枚・スリープ復帰・ロック画面でおかしくならない。
- この文書の「進捗ノート」と、リポジトリ直下の `README.md` の先頭に、ドパガキIMEの短い説明と入れ方を書く。

---

## 7. ビルドとインストールの手順

### 7.1 最初の準備（こーたのPCで1回だけ）
- Git と GitHub CLI を入れる（`winget install Git.Git`、`winget install GitHub.cli`）。
- `gh auth login` は**こーた本人が**ブラウザで行う（パスワードやトークンを Claude Code が入力しないこと）。
- リポジトリを置く：`gh repo clone koutatauko1-lab/mozc`。

### 7.2 毎回の流れ
1. コードを直す → 自分でよく見直す。
2. `git commit` → `git push`（ブランチは `master` でよい）。
3. `gh run watch` でビルドを見守る（失敗したら `gh run view --log-failed` で原因を読み、直して再 push）。
4. 成功したら `gh run download <run-id> -n Mozc64_x64.msi -D <作業用フォルダ>`。
5. `msiexec /i <作業用フォルダ>\Mozc64.msi` でインストール（管理者の確認画面は**こーたが**許可する）。
6. 新しい部品を確実に使うため、**一度サインアウトしてサインインし直す**（または PC を再起動）。最低でも、試すアプリを開き直し、タスクマネージャーで `mozc_renderer.exe` を終了させる。
7. Win＋スペースで Mozc に切り替えて試す。

### 7.3 ログの見方
- `OutputDebugStringW(L"[dopagaki] ...")` で出したログは、Sysinternals の DebugView（Microsoft 公式・無料）で見られる。こーたにお願いするときは手順を具体的に書くこと。

---

## 8. 守ってほしいこと（品質・安全）

- **入力を壊さない。** TIP の追加処理は、例外を投げない・待たない・失敗しても何もしないで戻る。キー処理の結果（`eaten`）を変えない。
- **候補ウィンドウの既存の動きを壊さない。** UI 命令の上書き事故（1.1節）に特に注意。
- **レンダラーを落とさない。** 新しい機能の初期化に失敗したら、その機能だけ止める。
- **軽く。** 何も起きていないときは CPU をほぼ使わない（タイマー停止・オーバーレイ非表示）。SSS で打ちまくっても、レンダラーの CPU 使用率は数%程度を目安にする。
- **他のアプリを動かさない。** 揺れは自分の描画と候補ウィンドウだけ。
- **打った文字の内容は保存も送信もしない。** 数えるのは回数だけ。
- 本家のコードスタイル（Google C++ スタイル、clang-format）に合わせる。新しいファイル名は `dopagaki_` で始める。
- コミットメッセージは英語で短く（例：`dopagaki: add combo counter wiring`）。

---

## 9. アップデート

フェーズ0〜7 のあとに足す機能。それぞれ別の指示書（仕様書）があるので、作業の前に最後まで読むこと。進捗はこの文書の末尾の「進捗ノート」に続けて書く。

- アップデート1「褒め言葉」：`docs/dopagaki/update1_praise.md`（コンボが5の倍数になるたびに英語の褒め言葉、200コンボごとに特大版。数値と動きの正解は新しい `docs/dopagaki/preview.html`）

---

## 付録A：`windows.yaml` の案（フェーズ0）

既存ファイルの `actions/checkout`・`actions/cache`・`actions/upload-artifact` のバージョン（コミットハッシュ指定）をそのまま使うこと。下はたたき台で、細部は実際に動かして調整してよい。

```yaml
name: CI for Windows

on:
  push:
    paths-ignore:
      - '**.md'
      - 'docs/**'
  workflow_dispatch:

permissions:
  contents: read

concurrency:
  group: ${{ github.workflow }}-${{ github.head_ref || github.ref }}
  cancel-in-progress: true

jobs:
  quick_check:
    runs-on: windows-2025
    timeout-minutes: 90
    steps:
      - uses: actions/checkout@<既存と同じ>
        with: { submodules: false, persist-credentials: false }
      - name: Restore update_deps cache
        uses: actions/cache@<既存と同じ>
        with:
          path: src/third_party_cache
          key: update_deps-${{ runner.os }}-${{ runner.arch }}-${{ hashFiles('src/build_tools/update_deps.py') }}
      - name: Install Dependencies
        shell: cmd
        working-directory: .\src
        run: python build_tools/update_deps.py
      - name: Restore bazel disk cache
        uses: actions/cache/restore@<既存の cache と同じコミット>
        with:
          path: bazel-disk-cache
          key: bazel-quick-${{ github.sha }}
          restore-keys: bazel-quick-
      - name: Build renderer and TIP, run dopagaki tests
        shell: cmd
        working-directory: .\src
        env: { ANDROID_NDK_HOME: "" }
        run: |
          bazelisk build --config release_build --disk_cache=%GITHUB_WORKSPACE%\bazel-disk-cache //renderer/win32:win32_renderer_main //win32/tip:mozc_tip64
          bazelisk test --disk_cache=%GITHUB_WORKSPACE%\bazel-disk-cache //renderer/win32:all --test_tag_filters=dopagaki
      - name: Save bazel disk cache
        if: always()
        uses: actions/cache/save@<既存の cache と同じコミット>
        with:
          path: bazel-disk-cache
          key: bazel-quick-${{ github.sha }}

  package:
    needs: quick_check
    runs-on: windows-2025
    timeout-minutes: 120
    steps:
      - uses: actions/checkout@<既存と同じ>
        with: { submodules: false, persist-credentials: false }
      - name: Restore update_deps cache
        uses: actions/cache@<既存と同じ>
        with:
          path: src/third_party_cache
          key: update_deps-${{ runner.os }}-${{ runner.arch }}-${{ hashFiles('src/build_tools/update_deps.py') }}
      - name: Install Dependencies
        shell: cmd
        working-directory: .\src
        run: python build_tools/update_deps.py
      - name: Restore built Qt
        id: qt-cache
        uses: actions/cache@<既存と同じ>
        with:
          path: src/third_party/qt
          key: qt-${{ runner.os }}-${{ runner.arch }}-${{ hashFiles('src/build_tools/build_qt.py', 'src/build_tools/update_deps.py') }}
      - name: Build Qt
        if: steps.qt-cache.outputs.cache-hit != 'true'
        shell: cmd
        working-directory: .\src
        run: python build_tools/build_qt.py --release --confirm_license
      - name: Delete Qt src to save disk space
        if: steps.qt-cache.outputs.cache-hit != 'true'
        shell: cmd
        working-directory: .\src
        run: rmdir third_party\qt_src /s /q
      # （任意）REVISION を run_number で上げる
      - name: Build installer
        shell: cmd
        working-directory: .\src
        env: { ANDROID_NDK_HOME: "" }
        run: bazelisk build package --config release_build
      - name: Upload Mozc64_x64.msi
        uses: actions/upload-artifact@<既存と同じ>
        with:
          name: Mozc64_x64.msi
          path: src/bazel-bin/win32/installer/Mozc64.msi
          if-no-files-found: error
```

- ドパガキの単体テストには `tags = ["dopagaki"]` を付けておく（上の `--test_tag_filters` で選ぶため）。テストがまだ無いフェーズ0では、テストの行は「該当なし」で失敗しないように調整すること。
- Qt のキャッシュを使うときは、`build_qt.py` がまた全部作り直さないことを確認すること。

---

## 付録B：プレビューとの対応表

| プレビュー（`preview.html`）の中身 | 実装する場所 |
|---|---|
| `RANKS` 配列（色・揺れ・粒数・光） | `dopagaki_state` / `dopagaki_effects` の定数 |
| `burst()`・`tick()` の粒の物理 | `dopagaki_effects` |
| `showBanner()` のキーフレーム | `dopagaki_effects`（バナー） |
| `breakCombo()` | BREAK イベントの処理 |
| `EDGE` 配列・`drawEdge()` | `dopagaki_effects`（ふち） |
| `.cand-wrap`・`.badge`・`li.sel` の CSS | 候補ウィンドウとオーバーレイ（枠・x42） |
| `SOUNDS`・`tone()`・`noise()` | `dopagaki_sound` |

---

## 進捗ノート

（Claude Code が更新する。日付・やったこと・次にやること・こーたに確認中のこと）

- 2026-10-16：指示書作成。フォーク作成済み、素の Mozc を CI でビルドしてインストール・動作確認済み（約45分）。Linux / macOS / Android / Lint のワークフローは GitHub 上で無効化済み。次はフェーズ0。
- 2026-10-16：**フェーズ0 完了**（こーた OK 済み）。
  - こーたのPCに GitHub CLI を入れ、`C:\Users\kouta\mozc` にクローン（`--filter=blob:none`）。git の名前・メールはこのリポジトリだけに設定。
  - `windows.yaml` を付録Aの形に。初回（run 2）は quick_check 20分＋package 56分（うち Qt 作り 25分）で成功。キャッシュ：Qt 77MB、Bazel ディスクキャッシュ 約0.2GB（quick）／約1.1GB（package、圧縮後）。ランナーの空きは C: 約29GB、D: 約143GB。
  - テストがまだ無いときは `bazel test` が終了コード4（テスト無し）になるので、成功扱いにしている。
  - REVISION の上げ方が付録の案では効かなかった（第6章フェーズ0の注意を参照）ので直した。
  - 2回目（run 3）は **19.6分**（quick_check 4分、package 15.5分、Qt はキャッシュで省略、ディスクキャッシュ 2599 件ヒット）。インストーラーのバージョンは `3.34.6345.1030` になった。
  - run 3 のインストーラーを `msiexec /i` で起動し、こーたが管理者確認を許可。`mozc_tip64.dll` などが他のアプリで使用中のため、インストールに約5分かかって最後に再起動を求められた（正常。再起動で使用中のファイルが入れ替わる）。再起動後は全ファイル `3.34.6345.1030`、メモ帳で入力・変換できることをこーたが確認。毎回こうなる前提で、こーたには「インストール中はほかのアプリを閉じておくとよい」と伝えてある。
  - 次はフェーズ1。開始前に K1〜K3 を確認する。
- 2026-10-16：**フェーズ1 完了**（こーた OK 済み：メモ帳・ブラウザで数える／アプリをまたいで続く／ランク変化／英数モード／変換の候補ウィンドウ／パスワード欄で数えない、すべて確認）。次はフェーズ2（開始前に K5 を確認）。
  - K1〜K3 はこーた確認済み（案どおり。変換・無変換キーは数える。長押しは試したうえで「数えない」のままでOK）。
  - 4.1〜4.5 と仮表示（`renderer/win32/dopagaki_debug_window.{h,cc}`：右下に「COMBO 12 / A」、5秒で消える、BREAK は赤）を実装。`dopagaki_state_test`（14ケース）は `quick_check` で合格。
  - 1回目（run 4、`3.34.6345.1040`）は**まったく数えられなかった**。原因：メモ帳・Brave では `OnTestKeyDown` が呼ばれない（4.2 参照）。`OnKeyDown` でも数えるように直した run 5（`3.34.6345.1050`）で、メモ帳で COMBO 表示・BREAK が動くことをこーたが確認。
  - 確認用の一時ログ（`OutputDebugStringW`、回数だけで打った内容は出さない）を TIP・送信スレッド・レンダラーに入れてある（`TEMPORARY (phase 1)` の印）。TIP は1打ごとに出すので、**フェーズ2で仮表示と一緒に消すか減らす**こと。
  - ログの取り方：DebugView の代わりに、`DBWIN_BUFFER` を読む小さな PowerShell スクリプトで記録した（非管理者で可）。TIP（メモ帳・Brave）のログは取れたが、**レンダラーのログは取れなかった**（表示は動いているので届いてはいる。レンダラー側の権限の違いと思われる。必要ならフェーズ2で調べる）。
  - インストールは毎回「約2〜5分＋最後に再起動」。
- 2026-10-16：**フェーズ2 完了**（こーた OK 済み：粒・K5 のコンボ表示・ランクアップのバナーと花火・BREAK の赤い光と文字と破片・クリックが下のアプリに届く・終わったら何も残らない、すべて確認）。
  - `dopagaki_effects`（純粋なロジック、テスト18ケース）と `dopagaki_overlay`（DirectComposition＋D3D11＋D2D）を実装。拡張スタイル `WS_EX_NOREDIRECTIONBITMAP | WS_EX_LAYERED | WS_EX_TRANSPARENT | …`＋`SetLayeredWindowAttributes(255)` で、**描画もクリックのすり抜けも実機で問題なし**（`UpdateLayeredWindow` 方式は不要だった）。
  - レンダラーは Mozc のサンドボックス（Low 整合性・制限トークン、ただし UI 制限なし）で動くが、D3D11/DComp は問題なく使えた。
  - 揺れは「自分で描いている演出すべて」にかけた（プレビューでは入力欄側だけが揺れるが、第3章の制約の書き方に合わせた）。候補ウィンドウの揺れはフェーズ4で。
  - 候補ウィンドウが出ているときはコンボ数を表示していない（「x42」はフェーズ4）。粒は候補ウィンドウの右上から出る。
  - フェーズ1の仮表示と、1打ごとの一時ログは削除した。残しているログは失敗時だけ（`[dopagaki] overlay ...`、`[dopagaki] sender ...`）。
  - ログの取り方：レンダラーは Low 整合性なので、`DBWIN_*` を Low ラベル付き（SDDL `D:(A;;GA;;;WD)S:(ML;;NW;;;LW)`）で作る記録スクリプトなら取れる（確認済み）。
  - 約6分試したあとのレンダラー：CPU 時間 約23秒、メモリ 約93MB。フェーズ7で長時間の測定をする。
- 2026-10-16：**フェーズ3 完了**（こーた OK 済み：A〜SSS のふちの光と火花・3秒でフェードアウトして次の1打で復活・BackSpace で消える、すべて確認。インストーラーは run 8、`3.34.6345.1080`）。エラーのログは無し。レンダラーは約40分で CPU 時間 約32秒、メモリ 約100MB。次はフェーズ4。
  - K4 はこーた確認済み（案どおり。消えるのは1秒かけて）。
  - `dopagaki_effects` にふちの状態（ランク・t・最後の打鍵からの時間）と火花を追加。テストは計26ケース。
  - 描き方：canvas の `shadowBlur` の代わりに、内側へ薄くなる16本の帯で光を描く。SSS の回る虹（canvas の conic gradient）は、各辺に沿った13色の線形グラデーションで近似。光（影）の色は SSS では `hsl(hue, 100%, 60%)` の1色（プレビューと同じ）。
  - ふちも揺れに合わせて動く（外側の線は画面の外まで伸ばして、揺れてもすき間が出ないようにした）。
  - 1回目のCI（run 7）で K4 のテストが失敗：16.67ms ずつ足した待ち時間が 4000ms ちょうどにならず、フェードが 0 にならなかった（実害はほぼ無いが直した）。
- 2026-10-16：**フェーズ4 完了**（こーた OK 済み：暗い候補ウィンドウ・区切り線・角丸・グラデーションの選択行（文字が読める）・光る外枠（SSS は虹が回る）・「x42」・一緒に揺れる・3秒で枠と x42 が消える・マウスと矢印キーで選べる、すべて確認。インストーラーは run 9、`3.34.6345.1090`）。
  - 色は `dopagaki_style.{h,cc}`（`ApplyDopagakiStyle`）でダークスタイルの上に上書き。`win32_dpi_util.cc` はダーク＋これを呼ぶだけ。用例ウィンドウ（infolist）もダークになる。
  - 選択行のグラデーションは両端を明るさ70%にした（明るい文字を読みやすくするため。2.4節で許されている調整）。選択行の枠線はやめた。
  - 外枠の光と「x42」はオーバーレイで描く。**K4 と同じく3秒で消える**ようにした（候補ウィンドウ自体の色はそのまま）。SSS の虹の枠は、角丸の枠を4方向に切り分けて各辺の線形グラデーションで近似。
  - 候補ウィンドウの揺れ：`UpdateLayout` で決めた位置（揺れなし）を覚えておき、オーバーレイの毎フレームの呼び出し（`SetFrameCallback`）で同じだけずらす。SSS の選択行は約30fpsで色相に合わせて描き直す。
  - エラーのログは無し。約9分試したあとのレンダラー：CPU 時間 約32秒（フェーズ3より多い。候補ウィンドウの描き直しと枠の描画が増えたため）、メモリ 約104MB。**フェーズ7で CPU を測って、必要なら減らす**。
- 2026-10-16：**フェーズ5 完了**（こーた OK 済み：1打ごとの音・ランクアップの音・BREAK の音・速く打っても途切れない・Windows の音量に従う・入力と変換は普通、すべて確認。インストーラーは run 12、`3.34.6345.1120`）。
  - 1回目（run 11）は**音が鳴らなかった**：レンダラーのサンドボックスで `CreateMasteringVoice` が `E_ACCESSDENIED`。こーたと相談し、サンドボックスはゆるめずに「TIP で鳴らす」形に変えた（4.8参照）。
  - run 10 は `mmreg.h` を `windows.h` より先に読んでコンパイルエラー（`xaudio2.h` を `windows.h` の後に置いて解決）。
  - 音の種類は今は「ピコピコ」固定（`WindowManager::dopagaki_sound_set_`）。フェーズ6でメニューから変える。
  - 残している小さな宿題：XAudio2 の既定デバイスが変わったとき（イヤホンを抜き差ししたときなど）の作り直しは、送信の失敗でしか検知していない。フェーズ7で確認する。
- 2026-10-16：**フェーズ6 完了**（こーた OK 済み：メニューの表示・7種の音の切り替えとチェック・ミュート・演出の ON/OFF・アイコンが壊れない・別のアプリでも同じ印・再起動しても設定が残る、すべて確認。インストーラーは run 13、`3.34.6345.1130`）。K6 は案どおり（ON/OFF も付ける）。
  - タスクバーの「あ」の右クリックメニューの最後に「ドパガキ演出」（チェック付き）と効果音7種（ラジオ）を追加。設定は mozc_server の設定（`dopagaki_config`）に保存されるので、PCを再起動しても残る（確認済み）。
  - メニューは `TipLangBarButton::OnClick` が毎回作っているので、そこでチェックを付ける（4.9参照）。入力モードの選択やアイコンには触らない。
  - OFF のときは演出も音も止め、コンボは 0 のまま（ON に戻すと 0 から）。仕様に書かれていなかったので自然な形で決め、こーたに報告した（異論なし）。
  - レンダラーは起動時に設定を読み、変更は `settings` だけの `DopagakiEvent` で届く。設定が変わるとログ `[dopagaki] settings: enabled=… sound_set=…` が出る（打った内容は含まない）。
  - 再起動後の設定ファイル（`LocalLow\Mozc\config1.db`）に `dopagaki_config` が「enabled=1, sound_set=5（メカ）」で保存されていることも確認。
  - こーたから「再起動後、打鍵音だけ少し小さくなった気がする（BackSpace の音は普通）」との声。調べた結果、不具合ではなく**音の種類の違い**：メカの打鍵音はプレビューのレシピどおりで、6種の中でいちばん小さい（最初の30msの音の強さで、ピコピコの打鍵音より約7dB小さい）。逆にメカの BREAK 音はピコピコより約7dB大きい。音の作り方のコードはフェーズ6で変えていない。打鍵音の高さはランクで上がるので、再起動直後（ランクB）はいちばん低く、少し弱く聞こえることもある。
  - こーたの指示で、メカの打鍵音の音量を 0.04 → 0.08（2倍、約+6dB）にした（ピコピコの打鍵音とほぼ同じ強さ）。この変更はフェーズ7の最初のインストーラーに入る。
- 2026-10-16：**フェーズ7 開始**。K7 はこーたの判断で「変える」（範囲は2か所だけ。3章の表を参照）。
  - IME の一覧の名前は、TIP の DLL の文字列リソースを Windows が読んで表示している（`SetLanguageProfileDisplayName`）。Windows は古い文字列を覚えていることがあるので、ID 201 の中身を変えるのではなく、新しい ID 202（`IDS_TEXTSERVICE_DISPLAYNAME_DOPAGAKI`）を作ってそちらを登録する。
  - 「アプリ」一覧の名前は MSI の `Package Name`。`UpgradeCode` は変えないので、上書きインストールはこれまでどおり。
  - フェーズ5の宿題（音の出口が変わったとき）：XAudio2 のコールバックは使わない方針（DLL が外れたあとに呼ばれる危険）なので、「3秒以上たっても再生が終わらない音がある＝エンジンが止まった」とみなして作り直すようにした（`kStalledSoundMs`）。イヤホンを抜き差ししても、最長3秒ほどで音が戻る見込み。実機で確認する。
  - リポジトリ直下の `README.md` の先頭に、ドパガキIME の説明と入れ方を書いた（その下はもとの Mozc の README）。
- 2026-10-16：**フェーズ7 完了**（こーた OK 済み）。インストーラーは run 16、`3.34.6345.1160`。**これで全フェーズ（0〜7）が終わった。**
  - こーたの確認：名前（Win＋スペースの一覧・「アプリ」一覧が「ドパガキIME」）・メカの音が大きくなった・入力と変換は普通・パスワード欄で数えない・モニター2枚・拡大率150%・全画面の動画で何も残らない・ロック画面・スリープ復帰、すべて問題なし。
  - 登録情報も確認：「アプリ」一覧は `ドパガキIME 3.34.6345.1160`、IME のプロファイルは `Description=Dopagaki IME`、`Display Description=@…\mozc_tip32.dll,-202`。
  - こーたの PC：Core i5-1235U（12スレッド）、Iris Xe、1920×1080 のモニター2枚（どちらも100%）。
  - CPU：何もしていないときは 0%。速く打ち続けたとき（SSS）の 5秒ごとの測定で、レンダラーは CPU 1コアの平均 約25%・最大 約61%（PC 全体ではそれぞれ 約2% と 約5%）。8章の目安「数%程度」に収まっているので、減らす作業はしていない。
  - 30分以上ふつうに使った記録（19:26〜19:58、30秒ごと）：レンダラーのメモリ 90.0→91.3MB、ハンドル 515〜523、GDI 15・USER 8 で変わらず。mozc_server も 6.6MB のまま。**増え続けるものは無し**。`[dopagaki]` のエラーのログも0件。
  - スリープ（19:16:43〜19:17:01）のあと、mozc_renderer と mozc_server は新しいプロセスで立ち上がり直していた（19:17:34）。アプリのエラーの記録（Windows のイベントログ）は無く、こーたも復帰後に普通に打てている。もとの Mozc の動きと思われる。
  - イヤホンの抜き差し（`kStalledSoundMs`）は、こーたが試したかどうか未確認。
  - 記録のスクリプト（`monitor.ps1`・`cpu_sampler.ps1`・`dbwin_listen2.ps1`）は、Claude Code のバックグラウンドで動かすとスリープで止まる。長い記録は `Start-Process powershell -WindowStyle Hidden` で別に動かすと止まらない。

### アップデート1「褒め言葉」

- 2026-10-17：**第0章**。こーたから指示書 `docs/dopagaki/update1_praise.md` と新しい `docs/dopagaki/preview.html`（上書き。354行→800行）を受け取って置いた。この文書に第9章「アップデート」を足した。
  - UK3 は**決定**：案どおり、同時に出る褒め言葉は3つまで（指示書の第2章の表も更新）。
  - Claude Code の週の使用枠が 94% まで来ていたので、こーたと相談して、**フェーズU1 は週の枠が戻ってから（2026-10-19 朝8時以降）始める**。こーたから声をかけてもらう。コードはまだ何も変えていない。
- 2026-10-19：**フェーズU1 完了**（こーた OK 済み：5ごとに出る・RANK!! と一緒に出て重ならない・打っている場所と候補ウィンドウに重ならない・ランクで強くなる・同時3つまで・BREAK で崩れ落ちる・モニター2枚・入力と変換は普通、すべて確認。インストーラーは run 18、`3.34.6345.1180`）。
  - 1回目の CI（run 17）はテストの書き方の間違い2つで失敗（200のあと300を試すテストで打鍵の回数を1から送り直していた／箱の高さを小数の誤差なしで比べていた）。本体は正しかった。
  - 確認中の記録：`[dopagaki]` のエラーは0件。
- 2026-10-19：UK2 は**決定**（今の大きさのまま。こーたが U1 の実際の画面で確認）。
- 2026-10-20：**フェーズU2 完了**（こーた OK 済み：1日使って確認、問題なし。インストーラーは run 19、`3.34.6345.1190`）。
  - 1日（約24時間）動かしたあとのレンダラー：CPU 時間 約240秒、メモリ 約114MB、ハンドル 682（フェーズ7の30分の記録は 約91MB・ハンドル 515〜523）。アプリのエラーの記録は無し。**増え続けていないかは U4 で30分以上の記録を取って確かめる。**
  - U1 と U2 の確認項目は、Claude の返事の書き方の間違いで、こーたの画面に出ていなかった（次からは確認項目を先に書く）。
- 2026-10-20：**フェーズU3 完了**（こーた OK 済み：褒め言葉の音・ランクアップ＋褒め言葉の音・BREAK の音を6セットで・200コンボの特大版・速く打っても途切れない・入力と変換は普通。インストーラーは run 20、`3.34.6345.1200`）。今回は確認項目を先に表示した。
  - 確認後のレンダラー（起動から約2時間20分、200コンボや6セットの試し打ちを含む）：CPU 時間 約277秒、メモリ 約126MB、ハンドル 632。アプリのエラーの記録は無し。U4 で30分以上の記録を取り、増え続けていないか確かめる。
  - 音：`DopagakiSoundKind` に `kPraise`(3)・`kRankUpPraise`(4)・`kMilestone`(5) を追加（0〜2 はそのまま）。`praise(r, d)` は6セットとも 1.11 の表どおり（`P = 2^(2r/12)`）。どれも1秒以内。選び方は BREAK ＞ 特大版 ＞ ランクアップ＋褒め言葉 ＞ ランクアップ ＞ 褒め言葉 ＞ 1打（今までは同じ打鍵でランクアップが BREAK より上だった）。TIP 側は復号と合成を呼んでいるだけなので変更なし。
  - 特大版：`DopagakiBannerKind::kMilestone`（1700ms）と下の言葉（袋から引く）。画面のランダムな場所で花火12回（各40粒・色相はばらばら）、白い光（0.35→0 を500ms、BREAK の赤い光とは別）、揺れ16。メインは虹のグラデーションで、光は白（18）と `hsl(色相)`（46）、影6。下の言葉は白で、光は `hsl(色相)`（18）と `hsl(色相+120)`（40）、影4。2行は画面の中央に縦に並べる（すき間は下の言葉の大きさ×0.15）。
  - 特大版のときも普通の褒め言葉が1つ出る（中央をよける）。
  - U1 の固定（ポンッ・ランク色）を外した。12種類（直前2回とは違うもの）・5色（重みで選び、同じ色なら1回引き直し）。
  - `DopagakiEffects`：衝撃波の輪（`rings()`、450ms）と衝撃の揺れ（slam・stamp）、scatter の最後の爆散（各文字から粒4つ）、バグッのノイズ（50msごとに引き直し）、1文字ずつの位置と乱数。アニメーション効果が OFF のときは輪と揺れを出さない（演出はフェードだけ）。
  - `DopagakiOverlay`：金（上から下）と虹（言葉全体の左から右。1文字の画像は言葉の中での位置に合わせて塗る）のグラデーション、ズドンの角丸の枠（光と影も一緒）、1文字ずつの画像（`HitTestTextPosition` で各文字の位置を測る）、バグッのシアンとマゼンタの色ずれ（光と影なし、加算合成）と3本の帯の横ずれ、シュッの残像、パタンの裏返しの暗さ。
  - 1文字ずつの演出は文字の数だけ画像を作る（ぼかし2回ずつ）。重さは U4 で測る。
  - `DopagakiState`：`kPraise`（5の倍数）と `kMilestone`（200, 300 …）を追加。まとめて届いた打鍵でも1つだけ。既存のテストのうち5の倍数をまたぐもの（10・120・5打の引き継ぎ）は、`kPraise` が増える形に直した。
  - 新しい `dopagaki_praise`：言葉（54＋8）・袋・演出12種＋フェードの情報とキーフレーム・1文字ずつの式・色の重み・大きさ・場所の決め方。`DopagakiCubicBezier` は `dopagaki_effects` からここへ移した（`dopagaki_effects.h` から読めるので使う側はそのまま）。
  - `DopagakiEffects`：`ChoosePraise`・`AddPraise`・同時3つ（4つ目で一番古いものが150msで消える）・BREAK で崩れ落ちる（止まって600msで落ちる＋赤い破片12個）・`Clear()` で消える。衝撃波と衝撃の揺れは U2、特大版は U3。
  - `DopagakiOverlay::AddPraise`：演出のフォントで測り（`GetOverhangMetrics` で文字のインクの上下を取り、上下に大きさの6%を足す）、幅が画面の70%を超えたら小さくする。文字の画像は出たときに1回だけ作る（光2重は「光の色」の文字から、影は黒0.85で下に4）。描くのは粒・ふち・候補ウィンドウの枠・コンボ表示のあと、BREAK の赤い光の前。
  - **U1 では演出はポンッ、色はランク色に固定**（`ChoosePraise` に固定の指定を渡している）。金と虹のグラデーションは U2。
  - よける場所：候補ウィンドウの周り30、カーソルの周り60（分からないアプリは「29 COMBO A」の位置から幅200・高さ50の周り30）、バナーが出ている間は中央、ほかの褒め言葉の周り10。
  - 音はまだ今までどおり（U3）。
