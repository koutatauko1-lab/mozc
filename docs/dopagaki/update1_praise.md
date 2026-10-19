# アップデート1：褒め言葉（Claude Code 向け指示書）

ドパガキIME（フェーズ0〜7 完了済み）に、**コンボが5の倍数になるたびに、英語の褒め言葉や応援が画面のどこかに出る**機能を足す。
この文書は、作業をする Claude Code のための指示書であり、仕様書でもある。

- 作業を始める前に、この文書を最後まで読むこと。`DOPAGAKI.md` の第0章（進め方のルール）と第8章（守ってほしいこと）は、このアップデートでもそのまま守る。特に「1フェーズ終わったら報告して止まる」「決まっていないことは実装前に聞く」「文字入力を絶対に壊さない」。
- 数値と動きの正解は、新しい `docs/dopagaki/preview.html`。「自動デモ」で200コンボまで流れ、「褒め言葉の演出」メニューと「1回だけ出す」ボタンで12種類をひとつずつ見られる。迷ったらプレビューのコード（`STYLES`・`drawPraise`・`drawLetters`・`pickPosition`・`showMilestone`・`SOUNDS.*.praise`）を読むこと。
- 進捗は `DOPAGAKI.md` 末尾の「進捗ノート」に「アップデート1」として続けて書く。

---

## 0. 最初にやること

1. こーたから受け取ったこの文書を `docs/dopagaki/update1_praise.md` に、新しいプレビューを `docs/dopagaki/preview.html` に置く（プレビューは上書き）。
2. `DOPAGAKI.md` に「## 9. アップデート」を足し、「アップデート1『褒め言葉』：`docs/dopagaki/update1_praise.md`」と書く。次の会話の自分がここにたどり着けるようにするため。
3. 1〜2 は docs だけの変更なので、CI のビルドは動かない（`paths-ignore`）。そのまま commit・push してよい。
4. 第2章の UK3 をこーたに確認してから、フェーズU1（第5章）を始める。

---

## 1. 決定済みの仕様（こーた承認済み・2026-10-17）

単位：長さは DIP（96dpi のときのピクセル）、時間は ms、角度は度。

### 1.1 いつ出るか

| 条件 | 出るもの |
|---|---|
| コンボが5の倍数になった（5, 15, 20, 25, 35 …） | 褒め言葉が1つ |
| ランクアップ（10, 30, 50, 100） | 今までの「◯ RANK!!」に加えて、褒め言葉も1つ（**両方出す**） |
| 200, 300, 400 …（100ごと、200から） | 画面中央に特大版「200 COMBO!!」＋特別な言葉（1.10節）。さらに普通の褒め言葉も1つ |

- 褒め言葉の段（強さ）は、その5の倍数のときのランク（10 なら A、100 なら SSS）。
- 打鍵がまとめて届いて5の倍数を2つ以上またいだときも、出すのは1つだけ（またいだ中で一番大きい倍数のもの）。特大版も同じ。
- BREAK のあとは0から数え直す（また5で出る）。
- パスワード欄など、数えない場所では出ない。「ドパガキ演出」が OFF のときも出ない。

### 1.2 言葉（全54種類＋特大版用8種類）

| 段 | 出るコンボ | 言葉 |
|---|---|---|
| B | 5 | Nice! / Good! / Cool! / Sweet! / Nice one! / Let's go! / Good start! / Warming up! |
| A | 10〜25 | Great! / Awesome! / Smooth! / Well done! / Way to go! / Keep it up! / You got this! / Go go go! / Nice combo! / Looking good! |
| S | 30〜45 | Excellent! / Amazing! / Fantastic! / Superb! / Brilliant! / Impressive! / Outstanding! / On fire! / So good! / Keep rolling! |
| SS | 50〜95 | Incredible! / Unbelievable! / Spectacular! / Phenomenal! / Magnificent! / Insane! / Wild! / Beast mode! / Crushing it! / Too fast! / Don't stop! / No way! |
| SSS | 100〜 | GODLIKE!! / LEGENDARY!! / UNSTOPPABLE!! / PERFECT!! / GENIUS!! / MASTERPIECE!! / MYTHICAL!! / UNREAL!! / SUPREME!! / MIND-BLOWING!! / OUT OF THIS WORLD!! / HALL OF FAME!! / TYPING GOD!! / ABSOLUTE LEGEND!! |
| 特大版 | 200, 300 … | BEYOND SSS! / ASCENDED! / TRANSCENDENT! / INFINITE POWER! / KEYBOARD MASTER! / RECORD BREAKER! / NO LIMITS! / ULTIMATE! |

- **袋から引く方式**：段ごとに袋を持ち、その段の言葉をひと通り出し切るまで同じ言葉は出さない。袋を詰め直すときはシャッフルし、詰め直した直後の1つ目が直前の言葉と同じにならないようにする（プレビューの `nextFromBag`）。特大版の言葉も同じ方式。
- 袋はレンダラーが動いている間だけ覚えていればよい（保存しない）。
- 言葉は英語の固定の文字列としてコードに持つ（ASCII のみ）。

### 1.3 演出（12種類）

- 出るたびに12種類から選ぶ。**直前の2回と同じ演出は選ばない**（残りの10種類から等確率）。
- 「傾き」は出るたびに一様乱数で決める固定の角度。「効果」の数字は長さに対する割合（0〜1）で、その時点で1回だけ起きる（1.7節）。

| ID | 名前 | フォント（無いときの代わり） | 長さ | 傾き | 効果 | 特徴 |
|---|---|---|---|---|---|---|
| pop | ポンッ | Arial Black（Segoe UI の Black） | 1300 | ±8 | 0：粒 | 弾むように出る |
| slam | ドンッ | Impact（Arial Black） | 1300 | ±8 | 0.1：衝撃 | 上から叩きつける |
| spin | くるっ | Segoe UI の Black・斜体 | 1300 | ±5 | 0.22：粒 | 回りながら出る |
| slide | シュッ | Bahnschrift の Bold・斜体・Condensed（Segoe UI の Bold・斜体） | 1300 | ±8 | 0.16：粒 | 残像つきで横から滑り込む（1.3.3） |
| drop | ボヨン | Arial Black | 1400 | ±8 | 0.18：粒 | 上から落ちてバウンド。下端を軸に伸び縮み |
| rise | ふわっ | Segoe UI の Black | 1500 | ±8 | 0：粒 | 浮かび上がる |
| jelly | ぷるん | Comic Sans MS の Bold（Segoe UI の Bold） | 1300 | ±8 | 0.12：粒 | ゼリーみたいに震える。下端を軸に伸び縮み |
| flip | パタン | Impact | 1300 | ±8 | 0.26：粒 | カードみたいにめくれる。裏返っている間（横の倍率が負）は不透明度×0.6 |
| glitch | バグッ | Consolas の Bold（Courier New の Bold） | 1300 | ±8 | 0：粒 | 色ずれと横ずれのノイズ（1.3.3） |
| wave | うねうね | Ink Free（Segoe Print → Segoe UI）。大きさ×1.1 | 1.3.2 | ±5 | 0：粒 | 1文字ずつ現れて波打つ |
| scatter | ババババ | Arial Black | 1.3.2 | ±5 | 最後に爆散（1.3.2） | 散らばった文字が集まり、最後に飛び散る |
| stamp | ズドン | Impact | 1300 | −12±4 | 0.12：衝撃 | ハンコのような角丸の枠つき（1.5節） |

#### 1.3.1 キーフレーム（文字全体で動く10種類）

書き方：プロパティごとに `(割合, 値, 次の区間の動き方)` を並べる（プレビューの `track()` と同じ）。

- o：不透明度、s：全体の倍率、sx・sy：横・縦の倍率（s と掛け合わせる）、r：回転、x・y：位置のずれ、k：横方向の傾け（x' = x + tan(k)·y）
- 書いていないプロパティは既定値（o・s・sx・sy は 1、r・x・y・k は 0）のまま。
- 最初のキーより前は最初の値、最後のキーより後は最後の値。
- 区間の動き方：`lin`（直線、省略時）、`in` = cubic-bezier(.42,0,1,1)、`out` = cubic-bezier(0,0,.58,1)、`io` = cubic-bezier(.42,0,.58,1)、`back` = cubic-bezier(.34,1.56,.64,1)（行き過ぎて戻る）。どれも既存の `DopagakiCubicBezier` で計算できる（`back` は 1 を超える値を返すのが正しい）。
- slide の x と k には、褒め言葉が画面の右半分にあれば +1、左半分にあれば −1 を掛ける（右半分なら右から来て左へ抜け、左半分なら左から来て右へ抜ける）。

```
pop     o : (0,0) (.1,1) (.8,1,in) (1,0)
        s : (0,.2,out) (.14,1.25,io) (.24,.95,io) (.32,1) (.8,1,in) (1,1.15)

slam    o : (0,0) (.06,1) (.8,1,in) (1,0)
        s : (0,3,in) (.1,.9,out) (.16,1.08,io) (.22,1) (.8,1,in) (1,.9)
        y : (0,-40,in) (.1,0) (.8,0,in) (1,10)

spin    o : (0,0) (.12,1) (.78,1,in) (1,0)
        s : (0,0,out) (.22,1.1,io) (.3,1) (.78,1,in) (1,.2)
        r : (0,-540,out) (.22,10,io) (.3,0) (.78,0,in) (1,360)

slide   o : (0,0) (.08,1) (.8,1) (1,0)
        x : (0,420,out) (.16,-20,io) (.24,0) (.8,0,in) (1,-320)      ×（右半分 +1／左半分 −1）
        k : (0,20,out) (.16,-8,io) (.24,0) (.8,0,in) (1,15)           ×（同上）

drop    o : (0,0) (.06,1) (.82,1,in) (1,0)
        y : (0,-260,in) (.18,0,out) (.26,-60,in) (.34,0,out) (.4,-18,in) (.46,0) (.82,0,in) (1,20)
        sx: (0,.9) (.18,1.25,out) (.26,.95) (.34,1.12) (.4,1) (.46,1.05) (.52,1)
        sy: (0,1.15) (.18,.75,out) (.26,1.08) (.34,.88) (.4,1) (.46,.95) (.52,1)

rise    o : (0,0) (.18,1,out) (.8,1) (1,0)
        y : (0,30,out) (.18,0) (1,-90)
        s : (0,.9,out) (.18,1)
        r : (0,-3,io) (.25,3,io) (.5,-3,io) (.75,3,io) (1,-3)

jelly   o : (0,0) (.06,1) (.82,1,in) (1,0)
        sx: (0,0,out) (.12,1.3,io) (.2,.8,io) (.28,1.15,io) (.36,.93,io) (.44,1.04,io) (.52,1) (.82,1,in) (1,1.4)
        sy: (0,0,out) (.12,.7,io) (.2,1.2,io) (.28,.85,io) (.36,1.07,io) (.44,.96,io) (.52,1) (.82,1,in) (1,0)

flip    o : (0,0) (.04,1) (.82,1,in) (1,0)
        sx: (0,0,out) (.1,-.85,in) (.18,0,out) (.26,1.1,io) (.32,1) (.82,1,in) (1,0)

glitch  o : (0,0) (.03,1) (.85,1) (1,0)

stamp   o : (0,0) (.05,1) (.82,1,in) (1,0)
        s : (0,2.6,in) (.12,.92,out) (.18,1.04,io) (.24,1)
```

#### 1.3.2 1文字ずつ動く2種類（wave・scatter）

- 長さ：`max(1500, 900 + 55 × 文字数)`。文字数は空白も含めた数（「OUT OF THIS WORLD!!」は19文字で1945ms）。
- T は出てからの時間（ms）、i は文字の番号（0から、空白も数える）、n は文字数。空白は描かない。
- 各文字の位置 lx は「言葉の中心から、その文字の中心までの横のずれ」（DirectWrite の `HitTestTextPosition` で各文字の左端と幅を取る）。
- clamp は 0〜1 に収めること、ease_back・ease_in・ease_out は 1.3.1 の back・in・out。

**wave（うねうね）**

```
a   = clamp((T - 35i) / 220)
ea  = ease_back(a)
o   = min(1, a / 0.4)
dy  = 34 (1 - ea)
s   = 0.6 + 0.4 ea
amp = 9 × clamp((T - 35i - 220) / 200)
dy += -amp × sin(2π T / 700 - 0.55 i)
t0  = 長さ - 300 - 20(n - 1)
e   = clamp((T - t0 - 20i) / 300)
o  *= 1 - ease_in(e)
dy += 40 × ease_in(e)
```

**scatter（ババババ）**：文字ごとに、出たときに乱数を1回だけ決めておく。

```
最初の位置：角度 θ0 は一様乱数、距離 d0 は 250〜400
            dx0 = cos θ0 × d0、dy0 = sin θ0 × d0 × 0.7
            rot0 = ±(90〜270)（符号も乱数）
飛び散る先：角度 θE は一様乱数、距離 dE は 260〜420
            dxE = cos θE × dE、dyE = sin θE × dE
            rotE = ±(180〜360)

a   = clamp((T - 15i) / 380),  ea = ease_back(a)
dx  = (1 - ea) dx0,  dy = (1 - ea) dy0,  rot = (1 - ea) rot0
s   = 0.4 + 0.6 ea,  o = min(1, a / 0.4)
e   = clamp((T - (長さ - 350)) / 350),  ee = ease_out(e)
dx += ee dxE,  dy += ee dyE,  rot += ee rotE
s  *= 1 + 0.3 ee,  o *= 1 - ease_in(e)
```

- T が「長さ − 350」を超えた瞬間に1回だけ、各文字の位置から粒を4つずつ飛ばす（速さ6、大きさ3、色は 1.4節の粒の色）。

#### 1.3.3 シュッ と バグッ の追加の描き方

**シュッ（slide）の残像**：本体より先に、35ms・70ms・105ms 前のキーフレームの状態で、不透明度×0.4・0.25・0.12 の残像を描く。残像と本体の位置がほぼ同じとき（|x の差| + 4×|k の差| < 3）は描かない。

**バグッ（glitch）**：0〜0.28 と 0.8〜1 の間（ノイズ区間）だけ、次のように描く。それ以外は普通に描く。

- 50ms ごとに次の乱数を引き直す：横ずれ ±10、傾け ±10°、ちらつき 0.4〜1（不透明度に掛ける）、帯ごとの横ずれ ±24（帯は文字の画像を上30%・中40%・下30%に切った3本）。
- 色ずれの量 d：t < 0.5 なら `2 + 12 × (1 - t / 0.28)`、それ以外は `2 + 12 × ((t - 0.8) / 0.2)`。
- 描く順番：①光と影の無いシアン（#00F0FF）の文字を −d ずらして、②同じくマゼンタ（#FF2BD6）の文字を +d ずらして、どちらも加算合成・不透明度 0.8×o×ちらつき、③本体を3本の帯に分け、帯ごとに横へずらして描く（不透明度 o×ちらつき）。ずらしは横ずれと傾けを足したあとの文字の座標で行う。

#### 1.3.4 変形のかけ方（プレビューの `place()` と同じ順番）

文字の画像の中心を原点として、次の順に変形する（先に書いたものから点にかかる。D2D の行列なら左から右へ掛ける）。

1. 下端を軸にする演出（drop・jelly）は、軸を文字の高さの半分だけ下に置いて、倍率（s×sx, s×sy）をかける。それ以外は中心で倍率をかける。
2. 横方向の傾け k（`D2D1::Matrix3x2F::Skew(k, 0)`。プレビューと向きが逆なら符号を反転）
3. 回転 r
4. 位置のずれ（x, y）
5. 出るときに決めた傾き（1.3節の表）
6. BREAK で崩れ落ちている間は、落下と回転（1.9節）
7. 褒め言葉の場所（1.6節）＋揺れ（既存の `shake_x()`・`shake_y()`）

1文字ずつの2種類は、1の代わりに「文字ごとの倍率 → 文字ごとの回転 → (lx + dx, dy) へ移動」をして、5〜7は同じ。

### 1.4 色（5種類）

出るたびに、段ごとの重みで選ぶ。直前の褒め言葉と同じ色になったら1回だけ引き直す。

| 段 | ランク色 | 白く光る | 反対色 | 金 | 虹 |
|---|---|---|---|---|---|
| B | 60 | 25 | 15 | 0 | 0 |
| A | 45 | 25 | 20 | 10 | 0 |
| S | 35 | 20 | 15 | 20 | 10 |
| SS | 25 | 15 | 15 | 25 | 20 |
| SSS | 20 | 10 | 10 | 30 | 30 |

| 色 | 文字の塗り | 光の色 | 粒の色 |
|---|---|---|---|
| ランク色 | ランク色（SSS は出たときの `hsl(色相, 100%, 62%)`） | 塗りと同じ | 塗りと同じ |
| 白く光る | #FFFFFF | ランク色 | ランク色 |
| 反対色 | B #FFA64D／A #E33BA6／S #3F6CFF／SS #4FFF76／SSS `hsl(色相+180, 100%, 62%)` | 塗りと同じ | 塗りと同じ |
| 金 | 上から下へのグラデーション：#FFF7CC（0）→ #FFD23F（0.5）→ #D18A0A（1） | #FFB300 | #FFD23F |
| 虹 | 言葉全体の左から右へのグラデーション：7色、k = 0〜6 で `hsl(h0 + 50k, 100%, 62%)`、h0 は出たときの色相 | #FFFFFF | 粒ごとに `hsl(h0 + 0〜300 の乱数, 100%, 62%)` |

- 「色相」は既存の `DopagakiEffects::hue()`（毎フレーム +3°）。
- 1文字ずつの演出でも、グラデーションは言葉全体を基準にする（文字ごとの画像に、言葉の中での位置に合わせた部分を塗る）。

### 1.5 大きさ・光・影・枠

- 文字の大きさ：段ごとに B 56／A 64／S 74／SS 86／SSS 100 に、`k = clamp(モニターの幅 / 1600, 0.5, 1.2)` と演出ごとの係数（wave だけ 1.1）を掛ける。1920 幅のモニターなら B 67〜SSS 120。
- 言葉の幅がモニターの幅の70%を超えたら、70%に収まるまで小さくする。
- 光：ぼかし2重。半径は `(10 + 2×段) × max(0.6, k)` と `(24 + 6×段) × max(0.6, k)`（段は B=0〜SSS=4）。色は 1.4節の光の色。
- 影：黒（不透明度 0.85）を真下に 4 ずらす。
- 描く順番は既存の `CreateGlowText` と同じ（光 → 影 → 文字）。ただし光の元になる文字は「光の色」で描く（塗りの色とは別にする）。
- ズドン（stamp）の枠：文字の周り（左右 18、上下 10 の余白）に、角の半径 12・太さ 5 の角丸の四角。色は文字の塗りと同じ（金・虹ならそのグラデーション）。光と影も文字と一緒にかける。

### 1.6 出る場所

モニター全体（オーバーレイの範囲）のどこにでも出る。ただし次をよける。

1. 候補ウィンドウ（`GetDopagakiCandidateWindowRect()`）の周り 30
2. 打っている場所：カーソル（`GetDopagakiCaretRect()`）の周り 60。カーソルが分からないアプリでは、「29 COMBO A」の表示位置（`GetDopagakiComboLabelPosition()`）から幅 200・高さ 50 の範囲の周り 30
3. 画面中央のバナー（RANK!!・BREAK・特大版）が出ている間は、横 20〜80%・縦 32〜68% の範囲
4. ほかに出ている褒め言葉（崩れ落ち中・消えかけ中のものは除く）の周り 10

決め方（プレビューの `pickPosition`）：

- 言葉の箱の大きさ＝傾きを入れて回した文字の箱 × 1.15 ＋ 20（幅 `(w cos + h sin) × 1.15 + 20`、高さ `(w sin + h cos) × 1.15 + 20`）。
- 画面の端から 16 以上離れ、箱が画面からはみ出さない範囲で、中心を一様乱数で16回まで試す。どれとも重ならない場所が見つかったらそこ。見つからなければ、重なりの面積の合計が一番小さかった場所。

### 1.7 粒・衝撃波・揺れ

- 粒（効果の「粒」）：言葉の中心から、数 B 6／A 10／S 16／SS 24／SSS 36、速さ `5 + 0.8×段`、大きさ `2.5 + 0.5×段`、色は 1.4節の粒の色。既存の `Burst` と同じ動き。
- 衝撃（効果の「衝撃」、slam・stamp）：
  - 衝撃波：言葉の中心に輪を描く。半径は 0.3w → 0.9w（w は言葉の幅）を 450ms で out の動き方。線の太さ 8 → 0、不透明度 0.8 → 0（どちらも直線）。色は粒の色（虹なら白）。加算合成。
  - 粒：数 B 8／A 12／S 18／SS 26／SSS 36、速さ `6 + 段`、大きさ `3 + 0.5×段`。
  - 揺れ：今の揺れと B 1／A 2／S 3／SS 5／SSS 7 の大きい方。

### 1.8 同時に出る数

- 同時に出ている褒め言葉は**3つまで**（UK3）。4つ目が出たら、一番古いものを 150ms で直線的に消す（消えかけ中のものは数えない）。

### 1.9 BREAK のとき

- 今までの BREAK の演出に加えて、出ている褒め言葉はすべて**崩れ落ちる**：
  - その瞬間の動きで止まる（キーフレームの時間を止める）。
  - 600ms かけて、下へ落ちる（落下量 `0.5 × 1667 × (経過秒)²`）、±30〜60° 回る（直線）、不透明度が 1 → 0（in の動き方）。
  - 崩れ始めに、言葉の中心から赤い破片（#FF2A3D、細長い四角）を12個（速さ8、大きさ4）。
- 「ドパガキ演出」が OFF になったときは、褒め言葉も含めてすべて即座に消える（既存の `Clear()`）。

### 1.10 特大版（200, 300, 400 …）

- 画面中央のバナーに「`200 COMBO!!`」（数字はそのときのコンボ）と、その下に特大版の言葉（1.2節）。バナーの枠は1つなので、出ている RANK!! や BREAK と同じく、新しいほうが置き換える。
- メインの文字：既存のバナーと同じフォント（Impact、無ければ Arial Black）。大きさは `clamp(モニターの幅 × 0.14, 56, 180)`。塗りは 1.4節の「虹」と同じグラデーション（h0 はそのときの色相）。光は白（半径18）と `hsl(h0, 100%, 60%)`（半径46）、影は下に 6。
- 下の言葉：大きさ `clamp(モニターの幅 × 0.06, 26, 76)`、メインの下に「下の言葉の大きさ × 0.15」のすき間。白い文字、光は `hsl(h0, 100%, 60%)`（半径18）と `hsl(h0+120, 100%, 60%)`（半径40）、影は下に 4。
- 動き（どちらも 1700ms、中央を軸に拡大縮小）：
  - メイン：全体の動き方は既存のランクアップと同じ cubic-bezier(.2,.8,.2,1)。`(0, 不透明度0, 倍率0.3) → (0.15, 1, 1.2) → (0.25, 1, 1) → (0.78, 1, 1) → (1, 0, 1.5)`
  - 下の言葉：直線。`(0, 0, 0.6) → (0.2, 0, 0.6) → (0.3, 1, 1.08) → (0.36, 1, 1) → (0.78, 1, 1) → (1, 0, 1.2)`
- 同時に：
  - 画面のランダムな場所で花火12回（各40粒、速さ9、大きさ4、粒ごとにランダムな色相 `hsl(0〜360, 100%, 62%)`）
  - 画面全体が白く光る：#FFFFFF、不透明度 0.35 → 0 を 500ms（ease-out）。BREAK の赤い光とは別に持つ。
  - 揺れ：今の揺れと 16 の大きい方
  - 普通の褒め言葉も1つ（1.1節。中央のバナーをよける）
  - 音は「特大版の音」（1.11節）

### 1.11 効果音

褒め言葉の音（以下 praise(r, d)）は、選んでいる効果音セットに合わせた短いファンファーレ。r は段（0〜4）、d は鳴り始めの遅れ（秒）、`P = 2^(2r/12)`（段ごとに2半音ずつ高くなる）。書き方は既存のレシピと同じ `tone(波形, 開始周波数, 終了周波数, 長さ, 音量, 遅れ)`・`noise(長さ, 音量, ハイパス, 遅れ)`。

| セット | praise(r, d) |
|---|---|
| ピコピコ | tone(square, 988P, 988P, .06, .06, d) ＋ tone(square, 1319P, 1319P, .12, .07, d+.06) |
| タイプライター | tone(sine, 2093P, 2093P, .35, .09, d) ＋ tone(sine, 4186P, 4186P, .2, .03, d) |
| 打撃 | t = 0 と .09 の2回：tone(sine, 180+20r, 50, .08, .3, d+t) ＋ noise(.05, .22, 1500, d+t) |
| 和太鼓 | tone(sine, 150+15r, 70, .15, .4, d) ＋ noise(.04, .25, 3000, d+.12) |
| キラキラ | f = 2637, 3136, 3951 を i = 0, 1, 2 として：tone(sine, fP, fP, .15, .05, d+.04i) |
| メカ | tone(sawtooth, 600P, 1800P, .12, .05, d) ＋ tone(square, 1800P, 1800P, .08, .03, d+.1) |
| ミュート | 鳴らさない |

新しい音の種類（1回の打鍵で鳴らすのは今までどおり1つ）：

| 種類 | 中身 |
|---|---|
| 褒め言葉の音 | praise(r, 0) |
| ランクアップ＋褒め言葉の音 | 既存のランクアップの音 ＋ praise(r, 0.25) |
| 特大版の音 | 既存のランクアップの音 ＋ praise(4, 0.32) ＋ praise(4, 0.5) |

優先順位：**BREAK ＞ 特大版 ＞ ランクアップ＋褒め言葉 ＞ ランクアップ ＞ 褒め言葉 ＞ 1打ごとの音**。

- 今までは同じ打鍵の中で「ランクアップ」が「BREAK」より優先だったが、BREAK を一番上にする（BackSpace のあとにまとめて届いた打鍵で10コンボを超えたときだけの違いで、実際にはまず起きない）。
- 褒め言葉が OFF のとき（UK1）は、「ランクアップ＋褒め言葉」の代わりに今までのランクアップの音。
- どの音も1秒以内に収まる（既存のテスト `AllSoundsAreAudibleAndSane` の条件）。TIP の `kStalledSoundMs`（3秒）より短いことも必ず守る。

### 1.12 Windows のアニメーション効果が OFF のとき

既存の `SetReducedMotion(true)` のとき：

- 揺れは今までどおり無し（褒め言葉の衝撃の揺れも無し）。衝撃波の輪も描かない。
- 演出は12種類の代わりに「フェード」だけ：フォントは Segoe UI の Black、長さ 1300、傾き 0、`o : (0,0) (.15,1) (.8,1) (1,0)` のみ。粒は出してよい。
- 言葉・色・場所・特大版・音はそのまま。

---

## 2. 仮決め（そのフェーズに入る前に、こーたに確認すること）

下は「こうするつもり」の案。確認して決まったら、この表を更新すること。

| # | 内容 | 案 | 関係するフェーズ |
|---|---|---|---|
| UK1 | 「あ」の右クリックメニューに「褒め言葉」の ON/OFF を付けるか | 付ける（初期は ON）。「ドパガキ演出」のすぐ下。OFF のときは、5ごとの褒め言葉も特大版も出ず、音もいつもの1打の音とランクアップの音だけ | U4 |
| UK2 | 実際の画面での大きさ | **決定**（2026-10-19、こーた OK）：プレビューと同じ計算（1.5節）のまま。U1 の実際の画面で、こーたがちょうどいいと確認した | U2 |
| UK3 | 同時に出る数 | **決定**（2026-10-17、こーた OK）：3つまで（プレビューと同じ） | U1 |

---

## 3. 設計（今のコードへの組み込み方）

### 3.1 全体の流れ

```
TIP（打鍵の数え方は変更なし。鳴らす音の種類が増えるだけ）
   │ DopagakiEvent（累計カウンタ）
   ▼
レンダラー
 DopagakiState::OnEvent
   → actions：kKeys, kRankUp, kMilestone（新）, kPraise（新）, kBreak
 WindowManager::OnDopagakiEvent
   ├ 鳴らす音を1つ選ぶ（1.11 の優先順位）→ RequestDopagakiSound（新しい種類も）
   ├ kKeys / kRankUp / kBreak → 今までどおり（BREAK では褒め言葉も崩れ落ちる）
   ├ kMilestone → DopagakiEffects::OnMilestone(n)
   └ kPraise   → DopagakiOverlay::AddPraise(段, よける場所)
                   ├ DopagakiEffects::ChoosePraise(段)：言葉・演出・色・文字の大きさを決める
                   ├ DirectWrite で言葉を測る（幅・高さ・各文字の位置）
                   └ DopagakiEffects::AddPraise(...)：場所を決めて登録
 DopagakiOverlay::DrawFrame
   └ 褒め言葉の文字の画像を（id ごとにキャッシュして）作り、毎フレーム変形して描く
```

- 演出の計算は今までと同じく、Windows API を使わない純粋なロジック（新規 `dopagaki_praise` と `dopagaki_effects`）に置いてテストする。
- 文字の画像は今までと同じく、出たときに1回だけ作り（`CreateGlowText` の拡張）、毎フレームは変形して貼るだけにする。

### 3.2 DopagakiState（`renderer/win32/dopagaki_state.{h,cc}`）

- `DopagakiAction::Type` に追加：
  - `kPraise`：`count` ＝ またいだ5の倍数の中で一番大きいもの、`rank` ＝ `GetDopagakiRank(count)`
  - `kMilestone`：`count` ＝ またいだ100の倍数の中で一番大きいもの（200以上のときだけ）、`rank` ＝ kSSS
- `AddKeys` で、足す前のコンボ `before` と足した後の `after` から判定する：`after / 5 > before / 5` なら kPraise（count は `after / 5 × 5`）、`after / 100 > before / 100` かつ `after / 100 × 100 >= 200` なら kMilestone。
- 1回の `AddKeys` が出す順番：`kKeys` → `kRankUp`（あれば）→ `kMilestone`（あれば）→ `kPraise`（あれば）。BREAK のあとに打鍵が足される場合も、この順番は `kBreak` のあと。
- 初めて見るセッションから最大5打を引き継ぐとき（既存）も、同じ判定で kPraise が出てよい。
- テスト（`dopagaki_state_test.cc` に追加）：
  - 1打ずつ 1〜5：5 のときだけ kPraise(5, B)
  - 9 → 10：kKeys、kRankUp(A)、kPraise(10, A) の順
  - まとめて 3 → 12：kPraise は1つだけ（count 10）。3 → 17 なら count 15
  - 99 → 100：kRankUp(SSS) と kPraise(100)。kMilestone は無し
  - 199 → 200：kMilestone(200) と kPraise(200, SSS)。299 → 300 も同様
  - BREAK のあとに5打：kBreak のあとに kPraise(5)

### 3.3 新規 `renderer/win32/dopagaki_praise.{h,cc}`（純粋なロジック）

1.2〜1.5節の表と計算をここに置く。Windows API は使わない。

- 定数：段ごとの言葉の配列（`std::wstring_view` か `const wchar_t*`）、特大版の言葉、段ごとの大きさ・粒の数・揺れ、色の重み、反対色。
- `enum class DopagakiPraiseStyle { kPop, kSlam, kSpin, kSlide, kDrop, kRise, kJelly, kFlip, kGlitch, kWave, kScatter, kStamp, kFade }`
- `enum class DopagakiPraiseColor { kRank, kWhite, kComplement, kGold, kRainbow }`
- `class DopagakiWordBag`：袋から引く方式（乱数関数を受け取る。テストで差し替える）。
- 演出の選び方（直前2回と違うもの）・色の選び方（重み＋同じなら1回引き直し）。
- 演出ごとの情報：フォントの候補（ファミリー名の並び・太さ・斜体・Condensed か）、長さ、傾きの範囲、効果の割合と種類、下端を軸にするか、残像、裏返しの暗さ、枠、ノイズ区間。
- キーフレーム：プロパティごとに `{割合, 値, 動き方}` の配列で持ち、`GetDopagakiPraiseFrame(style, t, side)` が `{opacity, scale_x, scale_y, rotation, offset_x, offset_y, skew_x}` を返す（s は scale_x・scale_y に掛け込んでおく）。
- 1文字ずつ：`GetDopagakiPraiseLetterFrame(style, i, n, T, duration, 文字ごとの乱数)` が `{opacity, scale, rotation, dx, dy}` を返す（1.3.2 の式）。
- 長さ：`GetDopagakiPraiseDurationMs(style, 文字数)`。
- 場所：`PickDopagakiPraisePosition(箱の幅, 箱の高さ, 画面の幅, 画面の高さ, よける箱の一覧, 乱数)`（1.6節）。
- テスト（新規 `dopagaki_praise_test.cc`、`tags` に `"dopagaki"` を付けて quick_check で動かす）：
  - 言葉の数が 8・10・10・12・14（合計54）、特大版が8。どれも空でなく ASCII だけ。
  - 袋：段の数だけ引くと全部の言葉がちょうど1回ずつ出る。詰め直しをはさんでも同じ言葉が続かない（1000回試す）。
  - 演出：1000回選んで、直前2回と同じものが一度も無い。12種類すべてが出る。
  - 色：B では金と虹が出ない。SSS では5色すべてが出る。同じ色が続く割合が下がっている。
  - キーフレーム：pop の t=0.14 で倍率 1.25、slam の t=0.1 で倍率 0.9・y=0、drop の t=0.18 で y=0・sx=1.25・sy=0.75、flip の t=0.1 で sx=−0.85、slide の t=0 で x=+420（右半分）／−420（左半分）、どの演出も t=1 で不透明度 0。
  - 1文字ずつ：wave の i=0 は T=0 で不透明度0、T=220 で1。scatter は T=380+15i 以降で dx・dy・rot がほぼ0（飛び散る前）。
  - 長さ：wave の19文字で1945、5文字で1500。
  - 場所：よける箱と重ならない場所がある場合は必ず重ならない。画面からはみ出さない。全部ふさがっているときも結果が返る。

### 3.4 DopagakiEffects（`renderer/win32/dopagaki_effects.{h,cc}`）

- 追加する状態：
  - `std::vector<DopagakiPraise> praises_`：言葉、演出、色、段、文字の大きさ、言葉の幅と高さ、文字ごとの lx と scatter 用の乱数、中心の位置、傾き、左右（slide 用）、経過時間、長さ、id（文字の画像のキャッシュ用、`next_id_` から）、色相 h0、起きた効果の数、バグッのノイズの状態（次に引き直す時刻と各値）、崩れ落ちの状態（開始からの時間・止めた時間・回る量）、消えかけの状態。
  - `std::vector<DopagakiRing> rings_`（衝撃波）
  - 白い光の経過時間（`white_flash_opacity()` で返す）
- 追加する関数：
  - `DopagakiPraiseChoice ChoosePraise(DopagakiRank tier)`：言葉・演出・色・文字の大きさ（1.5節。モニターの幅は `width()`）・h0 を決める。アニメーション効果が OFF なら演出はフェード。
  - `void AddPraise(const DopagakiPraiseChoice&, 言葉の幅, 高さ, 文字ごとの lx, const std::vector<DopagakiRect>& よける箱)`：言葉が幅の70%を超えるときは、呼ぶ側（オーバーレイ）が小さくしてから渡す。場所を決め（バナー中とほかの褒め言葉は自分で足す）、4つ目なら一番古いものを消しはじめる。
  - `void OnMilestone(uint64_t combo)`：バナー（新しい種類 `DopagakiBannerKind::kMilestone`、`DopagakiBanner` に `subtitle` を追加）・花火・白い光・揺れ。
  - 既存の `OnBreak()` に「褒め言葉の崩れ落ち＋赤い破片」を追加。
  - 既存の `Update()` で褒め言葉・衝撃波・白い光を進め、効果（粒・衝撃・爆散）を起こし、終わったものを消す。`Clear()` で全部消す。`IsActive()` は褒め言葉・衝撃波・白い光があれば true。
  - 特大版のバナーの動き：`GetDopagakiBannerFrame(kMilestone, ...)` と、下の言葉用の `GetDopagakiMilestoneSubtitleFrame(...)`。
- 粒は既存の `Burst()` を使う。虹の「粒ごとに色相を変える」ために、`BurstOptions` に色相の範囲（h0 と幅）を足すなどしてよい。
- テスト（`dopagaki_effects_test.cc` に追加）：
  - AddPraise で1つ増え、長さが過ぎると消え、`IsActive()` が false に戻る。
  - 4つ目を足すと、一番古いものが消えはじめ、150ms 後に消える。
  - slam は t=0.1 を過ぎたところで衝撃波が1つでき、揺れが段の値以上になる。アニメーション効果 OFF ではどちらも無い。
  - OnBreak で全部が崩れ落ちになり、600ms 後に消える。
  - OnMilestone(300) でバナーの文字が「300 COMBO!!」、下の言葉が特大版の言葉のどれか。白い光が 500ms で0になる。

### 3.5 DopagakiOverlay（`renderer/win32/dopagaki_overlay.{h,cc}`）

- `bool AddPraise(DopagakiRank tier, const std::vector<DopagakiRect>& avoid)`：`effects_.ChoosePraise()` → 演出のフォントで `IDWriteTextFormat` を作る（無いフォントは表の代わりへ。`HasFontFamily` を使う。Condensed は `DWRITE_FONT_STRETCH_CONDENSED`）→ `IDWriteTextLayout` で幅と高さ、`HitTestTextPosition` で各文字の左端と幅を測る → 幅が70%を超えたら小さくして測り直す → `effects_.AddPraise(...)`。失敗したら何もしない（入力にもほかの演出にも影響させない）。
- `CreateGlowText` を広げる（既存の呼び出しはそのまま動くように、引数を構造体にまとめるか、新しい関数を作る）：
  - 塗り：単色／金（縦グラデーション）／虹（横グラデーション。1文字の画像を作るときは、言葉全体の中でのその文字の位置だけずらす）
  - 光の色を塗りとは別に指定でき、2つの光で別の色も指定できる（特大版の下の言葉）
  - 影のずれ（褒め言葉は4）と不透明度（0.85）
  - 枠（ズドンの角丸の四角）
  - 光も影も無い単色（バグッの色ずれ用）
- 文字の画像のキャッシュ：`id` ごとに、言葉全体の画像1枚（バグッはさらにシアンとマゼンタの2枚）、または1文字ずつの画像（空白以外）。`DrawFrame` の最初に、無いものを作り、もう居ないものの分を捨てる。`ReleaseDevice()` で全部捨てる。
- 描き方（`DrawFrame` の中、粒・ふち・候補ウィンドウの枠のあと、BREAK の赤い光の前）：
  - 衝撃波（加算合成、`DrawEllipse`）
  - 褒め言葉：古い順に、1.3.4 の変形で画像を貼る。残像・ノイズの帯（`DrawBitmap` の元の範囲を指定）・色ずれ（加算合成）・裏返しの暗さを含む。
  - 白い光は赤い光と同じ場所で、画面全体を #FFFFFF で塗る。
  - 特大版のバナー：既存の `UpdateBannerCache` で、kMilestone のときはメインと下の言葉の2枚を作り、それぞれの動きで描く。
- 性能：文字の画像を作るのは褒め言葉が出たときだけ。1文字ずつの演出は文字の数だけ画像を作る（ぼかし2回ずつ）。U4 で CPU とメモリを測り、重ければ「言葉全体を1枚作って文字ごとに切り出す」方式に変えてよい（光が文字の境目で少し切れるのは許容）。

### 3.6 WindowManager（`renderer/win32/window_manager.{h,cc}`）

- `OnDopagakiEvent`：
  - 音：actions に kBreak・kMilestone・kRankUp・kPraise があるかを見て、1.11 の優先順位で `DopagakiSoundKind` を1つ選ぶ。
  - kMilestone → `effects.OnMilestone(action.count)`
  - kPraise → よける箱（1.6節の 1・2）を作って `dopagaki_overlay_->AddPraise(action.rank, avoid)`。カーソルの箱は `GetDopagakiCaretRect` → `ScreenToOverlay`。
  - actions はこの順に処理する（kRankUp と kMilestone のバナーを先に出してから kPraise の場所を決める）。
- 褒め言葉の ON/OFF（UK1 で付ける場合）：`ApplyDopagakiSettings` で `praise_enabled` を覚え、OFF なら kPraise と kMilestone を無視し、音は今までの選び方。

### 3.7 効果音（`renderer/win32/dopagaki_sound_synth.{h,cc}`）

- `DopagakiSoundKind` に `kPraise`、`kRankUpPraise`、`kMilestone` を足す（既存の値 0〜2 は変えない）。
- `DecodeDopagakiSoundRequest` の上限チェックを、一番最後の種類（kMilestone）にする。
- `SynthesizeDopagakiSound` に 1.11 の3種類を足す（praise(r, d) は内部の関数にして、ランクアップのレシピと組み合わせる）。
- TIP 側（`tip_dopagaki.cc`・`tip_dopagaki_sound.cc`）は、復号と合成を呼んでいるだけなので変更は不要のはず。TIP とレンダラーは同じインストーラーで同時に新しくなる。
- テスト：新しい3種類の符号化と復号の往復、`AllSoundsAreAudibleAndSane` のループに新しい種類を足す（1秒以内・聞こえる・有限の値）、「ランクアップ＋褒め言葉」はランクアップより長い、種類 7 はこれまでどおり不正。

### 3.8 設定メニュー（UK1 で付ける場合）

- `protocol/config.proto` の `DopagakiConfig` に `optional bool praise_enabled = 3 [default = true];`
- `TipLangBarCallback::ItemId` に `kDopagakiPraiseToggle = 58`、`tip_resource.h` に `IDS_DOPAGAKI_PRAISE_TOGGLE 148`、`tip_resource.rc` の日本語「褒め言葉」・英語「Praise words」。
- `tip_lang_bar.cc` のメニュー配列で「ドパガキ演出」の次に追加。`TipDopagakiMenu`（`IsMenuItem`・`GetMenuItemFlags`・`OnMenuSelect`）と `tip_text_service.cc` の `OnMenuSelect` の case に足す。既存の作り（`TipLangBarButton::OnClick` で毎回チェックを付ける、入力モードの選択やアイコンに触らない）をそのまま使う。
- 「ドパガキ演出」が OFF のときは、褒め言葉の設定に関係なく何も出ない。

### 3.9 ビルド設定

- `renderer/win32/BUILD.bazel` に `dopagaki_praise`（ライブラリ）と `dopagaki_praise_test`（`tags = MOZC_TAGS.WIN_ONLY + ["dopagaki"]`）を追加し、`dopagaki_effects` の deps に足す。
- 新しいシステムライブラリは不要（D2D・DirectWrite は既存）。

---

## 4. 触るファイルの一覧

| ファイル | 内容 |
|---|---|
| `docs/dopagaki/update1_praise.md`（新規）・`docs/dopagaki/preview.html`（差し替え）・`DOPAGAKI.md` | この文書、新しいプレビュー、第9章と進捗ノート |
| `renderer/win32/dopagaki_state.{h,cc}`・テスト | kPraise・kMilestone |
| `renderer/win32/dopagaki_praise.{h,cc}`・テスト（新規） | 言葉・袋・演出・色・キーフレーム・場所 |
| `renderer/win32/dopagaki_effects.{h,cc}`・テスト | 褒め言葉・衝撃波・崩れ落ち・特大版・白い光 |
| `renderer/win32/dopagaki_overlay.{h,cc}` | 測る・文字の画像・描く |
| `renderer/win32/window_manager.{h,cc}` | 音の選び方・褒め言葉と特大版の呼び出し |
| `renderer/win32/dopagaki_sound_synth.{h,cc}`・テスト | 新しい3種類の音 |
| `renderer/win32/BUILD.bazel` | 新しいライブラリとテスト |
| （UK1）`protocol/config.proto`、`win32/tip/tip_lang_bar_callback.h`・`tip_lang_bar.cc`・`tip_dopagaki_menu.{h,cc}`・`tip_text_service.cc`・`tip_resource.h`・`tip_resource.rc` | 褒め言葉の ON/OFF |

---

## 5. フェーズ

どのフェーズも、終わったら CI のインストーラーをこーたに入れてもらい、「できたこと」「こーたが確かめること」を報告して止まる。

### フェーズU1：5ごとに出る（ポンッだけ）

- 3.2・3.3・3.4・3.6（音以外）と、3.5 の「言葉全体の画像を作って貼る」部分。
- このフェーズでは、演出は**ポンッ**、色は**ランク色**に固定する（`ChoosePraise` の結果を上書き）。言葉の袋・場所・同時3つ・崩れ落ちは本番どおり。
- 開始前に UK3 を確認。
- **確認**：5・15・20…で褒め言葉が出る／10・30・50・100 では RANK!! と一緒に出て、RANK!! と重ならない／打っている場所と候補ウィンドウに重ならない／ランクで言葉が強くなる／BREAK で崩れ落ちる／モニター2枚でも打っている側に出る／入力と変換は今までどおり。

### フェーズU2：演出12種・色5種

- 固定を外し、12種類すべて（残像・ノイズ・1文字ずつ・枠）、5色（グラデーション）、フォント、衝撃波と衝撃の揺れ、アニメーション効果 OFF のときのフェード。
- 開始前に UK2 を確認（実際の画面で大きさを見てもらう）。
- **確認**：プレビューの「褒め言葉の演出」で同じ名前を選んだときと見比べて、12種類それぞれが同じ雰囲気／金と虹がきれい／文字が読める。

### フェーズU3：音と特大版

- 3.7（新しい3種類の音）と 1.10（特大版）。
- **確認**：褒め言葉の音・ランクアップ＋褒め言葉の音・BREAK の音が、6つのセットそれぞれでプレビューと近い／200コンボで特大版（白い光・花火・揺れ・音）／速く打っても音が途切れない。

### フェーズU4：メニューと仕上げ

- 開始前に UK1 を確認し、付けるなら 3.8。
- SSS で打ち続けたときのレンダラーの CPU とメモリを、フェーズ7と同じ方法で測る（30分以上、増え続けるものが無いこと）。重ければ 3.5 の最後の方法で軽くする。
- `README.md` のドパガキIME の説明に「5コンボごとの褒め言葉」と「200コンボごとの特大版」を一言足す。
- **確認**：メニューの ON/OFF（付けた場合）・再起動しても設定が残る・CPU とメモリ・入力と変換は今までどおり。

---

## 6. 守ってほしいこと（このアップデートで特に）

- `DOPAGAKI.md` 第8章をそのまま守る（入力を壊さない・候補ウィンドウを壊さない・レンダラーを落とさない・軽く・他のアプリを動かさない・打った内容を保存しない）。
- 褒め言葉の言葉は打った内容と関係なく選ぶ。打った文字を表示に使わない。
- 褒め言葉の準備（フォント・測る・画像作り）が失敗しても、ほかの演出・音・入力には影響させない（その褒め言葉を出さないだけ）。
- 新しい音は1秒以内（3秒の `kStalledSoundMs` より十分短く）。
- 文字の画像は使い終わったら必ず捨てる（長時間使ってもメモリが増え続けないこと）。

---

## 付録：プレビューとの対応表

| プレビュー（`preview.html`）の中身 | 実装する場所 |
|---|---|
| `PRAISE_WORDS`・`MILESTONE_WORDS`・`nextFromBag` | `dopagaki_praise`（言葉・袋） |
| `STYLES`・`FADE_STYLE`・`track()`・`EASE` | `dopagaki_praise`（演出・キーフレーム） |
| `pickStyle`・`pickColorKind`・`COLOR_WEIGHTS`・`COMPLEMENT`・`makePaint` | `dopagaki_praise`（演出と色の選び方） |
| `spawnPraise`（大きさ・傾き・場所） | `DopagakiEffects::ChoosePraise`・`AddPraise`・`DopagakiOverlay::AddPraise` |
| `pickPosition` | `PickDopagakiPraisePosition` |
| `makeSprite`・`fillStyleFor` | `DopagakiOverlay` の `CreateGlowText`（拡張） |
| `drawPraise`（`place()`・残像・ノイズ・裏返し） | `DopagakiOverlay::DrawFrame` |
| `drawLetters` | `GetDopagakiPraiseLetterFrame` と `DopagakiOverlay::DrawFrame` |
| `firePraiseFx`・`rings` | `DopagakiEffects`（効果・衝撃波） |
| `crumblePraises` | `DopagakiEffects::OnBreak` |
| `showMilestone` | `DopagakiEffects::OnMilestone`・`DopagakiOverlay::UpdateBannerCache` |
| `SOUNDS.*.praise`・`playRankUpPraise`・`playMilestone` | `dopagaki_sound_synth`（新しい3種類） |
| `hit()` の中の `praise`・`milestone` の判定 | `DopagakiState::AddKeys` |

（プレビューだけのもの：「褒め言葉の演出」メニュー・「1回だけ出す」ボタン・「直前の褒め言葉」の表示。実装しない。）
