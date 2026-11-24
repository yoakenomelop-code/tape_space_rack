# Tape Space Rack（AU / VST3 / Web）

**テープ・シミュレーター、ディレイ、リバーブ**を、1 つのラック画面で好きな数だけ並べて使えるプラグインです。
画面の作りは Vintage Rack / Bus Comp Rack と同じで、テープの飽和カーブ、エコーの並び、残響の減衰、帯域ごとの残響時間、周波数特性を目で見ながら調整できます。
Web 版と同じ DSP（音のアルゴリズム）を C++ に移植しています。

各モジュールは往年のスタジオ機材の一般に知られた動作を参考に独自に設計したもので、特定の製品やメーカーとは一切関係がありません。

## 入っているモジュール

| モジュール | モデル | 主なつまみ |
|---|---|---|
| **Tape Machine** | Studio 2-Track ／ Multitrack ／ Cassette | INPUT、FORMULA（MODERN / CLASSIC / VINTAGE）、BIAS、SPEED 3.75 / 7.5 / 15 / 30 ips、EQ NAB / CCIR、WOW、FLUTTER、HISS、MIX、OUTPUT。マシン画面で電子回路（真空管 / ソリッドステート / トランスレス）、ヘッド（新品 / 摩耗 / 研磨）、録音レベル（標準 / +3 / +6 dB）、HEADROOM、AGE |
| **Echo / Delay** | Digital HQ ／ Tape Echo ／ Analog BBD ／ Vintage Digital | TIME（Digital HQ は〜8 s）、SYNC（79 種、下記）、FEEDBACK（〜110 %）、LOW / HIGH CUT（ループ内）、DRIVE、MONO / STEREO / **PING-PONG** / DUAL、WIDTH、**REVERSE**、MOD RATE / DEPTH、DUCK、**WET EQ**（LOW / MID・周波数 / HIGH）、Tape Echo は再生ヘッド 1〜3 の組み合わせ |
| **Space Reverb** | ALGO（Hall / Room / Plate / Chamber / Cathedral）／ **IR**（内蔵 7 種）／ **USER IR**（ファイル読み込み） | PRE-DELAY（〜1 s・SYNC）、SIZE、DECAY（SYNC）、LOW ×・HIGH ×（帯域ごとの残響時間）、DIFFUSION、ER、MOD、**REVERSE**（長さ 0.1〜6 s・SYNC）、IR LENGTH、LOW / HIGH CUT、WET EQ、WIDTH、MIX、OUTPUT |

プラグインは 1 種類（**Tape Space Rack**、AU コード `aufx TSRk VtRk`）で、最大 8 台まで追加・複製・削除・並べ替えができます。AU（Mac）、VST3（Mac / Windows）、Standalone が作られます。レイテンシは 0 です。

### テンポ同期（SYNC）と精密な時間設定
- ディレイの TIME、リバーブの PRE-DELAY・DECAY（RT60）・REVERSE LENGTH を、それぞれ独立にテンポへ同期できます。プラグインではホストの BPM に追従します（Standalone と Web 版は BPM を入力）。
- 音符は **79 種**：None（同期なし）、1/64〜1/2 の 3 連符・通常・付点、1 小節の 3 連符・通常・付点、2・3・4 小節、4/3・5/4 小節、そして **n/64 刻み**（5/64〜63/64。約分できるものは 14/64 (7/32) のように併記）。
- SYNC つまみの値をクリックすると、79 種を 4 列に並べた一覧が開きます。検索（「7/32」「dot」「trip」など）、QUICK JUMP（1/64・1/8・1 BAR・n/64・現在値）、矢印キー＋Enter で選べ、下の行に今の BPM での長さ（ms）が出ます。
- 時間のつまみ（TIME、PRE-DELAY、DECAY、REVERSE LENGTH など）は、値をクリックすると数値を直接入力できます（例：`437.5`、`1.25 s`）。0.001 ms 単位で設定でき、同期中は同期後の長さを表示します。

### ディレイのプリセット（23 種）
高品位なデジタルからアナログまで用意しています。
- **Digital HQ**：Quarter（Sync）、Dotted Eighth、Slapback、Wide Dual、Ping-Pong（8 分・3 連）、Reverse Swell、Reverse Ping-Pong、Telephone Echo（ウェット EQ）、Dark Ambient Wash、Fine 5/16 Polyrhythm、Fine 7/32 Push、Bar-long Swell、Ducked Vocal Echo
- **Tape Echo**：Single Head、Multi-Head、Runaway（発振ぎみ）、Rockabilly Slap
- **Analog BBD**：Warm、Chorus-Echo、Long Murky
- **Vintage Digital**：12-bit Rack、Digital Dub

### リバーブのプリセット（21 種）
アルゴリズム 9 種（Concert Hall、Large Hall、Small Room、Drum Room、Vocal Plate、Bright Plate、Chamber、Cathedral、Infinite Pad）、テンポ同期 2 種（Tempo Plate：残響 1 小節、Tempo Hall：プリディレイ 1/32・残響 2 小節）、内蔵 IR 7 種、リバース 3 種（Reverse Hall Swell〈1/2 拍同期〉、Reverse Plate Lead-in、Reverse Church IR〈1 小節同期〉）。

### テープのプリセット（13 種）／ラックプリセット（9 種）
30 ips マスター、15 ips NAB / CCIR、マルチトラックのドラム、7.5 ips の飽和、3.75 ips ローファイ、カセット、バイアスのアンダー／オーバーなど。ラックプリセットはテープ → ディレイ → リバーブの組み合わせ（Modern Vocal、Dub Station、Reverse Dream、Church Ping-Pong など）です。DAW のプログラム一覧からも選べます。

## 高音質のための処理

- **すべて倍精度（64 bit 浮動小数点）**で計算しています。
- **テープ**：飽和は 2 倍オーバーサンプリング（10 係数・楕円特性のポリフェーズ IIR ハーフバンド、阻止域 −116 dB、追加レイテンシなし）。録音 EQ（プリエンファシス）→ テープ飽和 → 再生 EQ（完全な逆特性）の順に処理するので、大きな音ほど高域から先に詰まる、テープらしい周波数依存の飽和になります。
- **ディレイ**：読み出しは 4 点エルミート補間。Digital HQ は時間を変えると 50 ms のクロスフェードで切り替わり（ピッチが揺れない）、テープ／BBD は実機のように時間変更でピッチが滑ります。BBD は時間に応じてクロック（＝帯域）が変わります。発振させてもリミッター／素子の飽和で頭打ちになり、暴走しません。
- **リバース・ディレイ**：TIME の長さで区切った音を、半周期ずらした 2 つの読み出しヘッド（sin² 窓、和が常に 1）で逆再生します。つなぎ目でクリックが出ません。
- **アルゴリズム・リバーブ**：16 本のディレイラインのフィードバック・ディレイ・ネットワーク（アダマール行列で混合、長さは素数）、入力拡散 4 段、初期反射 24 タップ、ラインごとの揺れ（エルミート補間）。ラインごとに低域・中域・高域の減衰を設計し、DECAY・LOW ×・HIGH × で指定した残響時間（RT60）どおりに減衰します（実測で誤差 2 % 以内）。
- **IR リバーブ**：遅延ゼロの分割畳み込み。先頭 128 サンプルは直接畳み込み、続く 2048 サンプルまでは 128 サンプル単位、以降は 2048 サンプル単位の FFT で、重い計算を 128 サンプルごとに分散します。直接計算した畳み込みとの差は −280 dB 以下。IR の構築・切り替えは別スレッドで行い、約 80 ms のクロスフェードで差し替えます。
- **USER IR**：WAV / AIFF / FLAC を読み込み（最長 12 秒）、ホストのサンプルレートへ窓付き sinc 補間で変換します。読み込んだ IR はプロジェクト（プラグインの状態）に埋め込まれるので、元のファイルを消しても再現できます。
- **リバース・リバーブ**：IR（ALGO の場合はアルゴリズムのインパルス応答を一度レンダリングしたもの）を時間反転して畳み込みます。音の後ろで REVERSE LENGTH をかけて膨らみ、ぷつっと切れる効果です。

## 画面の見かた

- **FREQUENCY MAP**：白はテープを通った音の周波数特性（ヘッドバンプ・高域・バイアス）、緑の点線はディレイの反復音の音色（ループフィルター＋ウェット EQ）、紫の点線はリバーブ残響の音色、灰色は出力スペクトル。縦軸は ±24 / ±12 / ±4 dB。
- **Tape Machine の下段**：IN → OUT（1 kHz の入出力。白い点は今の入力）、周波数特性と入力レベル（−28 dBFS と −6 dBFS の比較。大音量で高域が圧縮される様子）、レベル履歴。
- **Echo / Delay の下段**：エコーの並び（クリックを入れたときの反復。上が L、下が R。ピンポン・リバース・マルチヘッドの違いが見えます）、エコー履歴。
- **Space Reverb の下段**：残響の減衰（エネルギーの時間変化、−60 dB までの時間。リバースは膨らみ方）、帯域ごとの残響時間（ALGO）または IR 波形（IR）、残響履歴。
- つまみにカーソルを合わせると、関係する表示が同じ色で光り、ヒント行に説明が出ます。
- **ANALOG**（テープ・ディレイ）：オンで飽和・揺れ・ノイズを加えます。オフで音色（EQ・フィルター）だけ。
- **BPM**：プラグインではホストのテンポに自動で追従します（Standalone と Web 版は手入力。Web 版は TAP も可）。

## Web 版

`web/index.html`（1 ファイル）をローカルサーバー経由で開くと、ブラウザで同じ音を試せます（AudioWorklet のため `file://` では動きません）。
```bash
cd web && python3 -m http.server 8000   # → http://localhost:8000/index.html
```
デモ音源（シンセ・フレーズ／ボーカル風リード／ドラム／バンドミックス）か、手元の音声ファイルで試せます。リバーブの USER IR も読み込めます（Web 版ではページを閉じると消えます）。
`web/app.html` と `web/dsp.js` を編集したら `python3 web/build.py` で `index.html` を作り直します。

## ビルド方法

### いちばん簡単：GitHub Actions（Mac / Windows 両方）
push すると **Actions** タブで `build` が自動で走ります（20〜30 分程度）。終わったら `TapeSpaceRack-mac`（AU + VST3）と `TapeSpaceRack-windows`（VST3）をダウンロードします。Mac 版は CI 上で pluginval による検証も実行されます。

### GitHub Actions でビルドしたものを Mac で使う
1. `TapeSpaceRack-mac` の中の `TapeSpaceRack-mac.zip` を展開し、`Components` と `VST3` の中身をプラグインフォルダにコピーします。
   - 自分のユーザーだけ：`~/Library/Audio/Plug-Ins/Components` と `~/Library/Audio/Plug-Ins/VST3`
   - Mac 全体：`/Library/Audio/Plug-Ins/Components` と `/Library/Audio/Plug-Ins/VST3`
2. 正式な署名がないため「Mac を保護するためにブロックされました」と出ます。ターミナルで次を実行してください（最初に Mac のログインパスワードを聞かれます）。処理したものは `OK:` と表示されます。
```bash
for d in "$HOME/Library/Audio/Plug-Ins" "/Library/Audio/Plug-Ins"; do
  for p in "VST3/Tape Space Rack.vst3" "Components/Tape Space Rack.component"; do
    [ -e "$d/$p" ] || continue
    sudo xattr -dr com.apple.quarantine "$d/$p"
    sudo chmod +x "$d/$p"/Contents/MacOS/*
    echo "OK: $d/$p"
  done
done
killall -9 AudioComponentRegistrar 2>/dev/null
```
3. DAW を起動し直してプラグインを再スキャンします（Ableton Live は 設定 → プラグイン で Option キーを押しながら「再スキャン」）。

### Mac で自分でビルド
必要なもの：Xcode、CMake（`brew install cmake`）
```bash
cmake -B build -G Xcode -DTS_COPY_AFTER_BUILD=ON
cmake --build build --config Release
```
初回は JUCE 8.0.4 を自動ダウンロードします。AU が見えない場合は `killall -9 AudioComponentRegistrar` のあと `auval -v aufx TSRk VtRk`。

### Windows で自分でビルド
必要なもの：Visual Studio 2022（「C++ によるデスクトップ開発」）、CMake
```bat
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```
`build\TapeSpaceRack_artefacts\Release\VST3\Tape Space Rack.vst3` を `C:\Program Files\Common Files\VST3` にコピーします。

## 確認済みのこと
- C++ 版と Web 版の DSP 出力が一致すること（3 モジュール・75 設定、時間変更の途中切り替え・テンポ同期 [97.3 BPM] を含めて差 −144 dB 以下）。79 種の音符表も C++ と Web で完全に一致
- 分割畳み込みが直接計算と一致し（−280 dB 以下）、遅延がゼロであること
- アルゴリズム・リバーブの残響時間が設定どおりであること（0.5 / 2.2 / 6 秒、全モデルで誤差 2 % 以内）
- ディレイの時間・テンポ同期・ピンポン・リバースが正しく並ぶこと、リバースのつなぎ目にクリックがないこと
- スロットの追加・複製・移動・削除、USER IR の読み込み・サンプルレート変換・状態への埋め込みと復元、44.1 / 48 / 96 kHz、NaN が出ないこと
- pluginval（strictness 5）で VST3 が合格すること（Linux）

## ライセンス

このリポジトリのソースコードは [GNU Affero General Public License v3.0](LICENSE)（AGPLv3）で公開しています。
- 使用しているフレームワーク [JUCE](https://juce.com) は AGPLv3 と JUCE ライセンスのデュアルライセンスです。本リポジトリは AGPLv3 の条件で JUCE を利用しています。ソースを公開せずに配布・販売する場合は、JUCE ライセンスが別途必要です。
- VST は Steinberg Media Technologies GmbH の商標です。Audio Units は Apple Inc. の商標です。対応形式を示す目的でのみ記載しています。
- 内蔵の IR はすべてプログラムで合成したもので、既存の録音や製品のサンプルは含んでいません。

## 参考にした機材について

各モジュールは、次の種類の機材の一般に知られた動作を参考に、独自に設計・実装したものです。特定の製品の回路や測定データを再現したものではありません。

| モジュール | 参考にした機材の種類 |
|---|---|
| Tape Machine | 1/4 インチ 2 トラックのスタジオ用オープンリール、2 インチ・マルチトラックレコーダー、カセットデッキ |
| Echo / Delay | ラック型デジタルディレイ、マルチヘッドのテープエコー、BBD（バケツリレー素子）のアナログディレイ、1980 年代の 12 bit デジタルディレイ |
| Space Reverb | コンサートホール・部屋・エコーチェンバー・教会などの空間、鉄板（プレート）リバーブ、スプリングリバーブ |
