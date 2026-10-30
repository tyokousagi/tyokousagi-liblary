# tyokousagi-liblary

AtCoder Heuristic Contest（AHC）で使う C++ のライブラリと、ローカルでの実行結果を調べる Python のスクリプトです。

## ファイル一覧

| ファイル | 中身 |
|---|---|
| `template.cpp` | 提出コードの雛形。時間の計測、乱数、パラメータの読み込み（`param`）、デバッグ出力（`dump`） |
| `annealing.cpp` | 焼きなまし（`SimulatedAnnealing`）、近傍の選択（`NeighborSelector`）、停滞の検出（`StagnationDetector`） |
| `beamsearch.cpp` | Euler Tour を使う差分更新ビームサーチ。重複除去とビーム幅の自動調整つき |
| `grid.cpp` | グリッドの部品（`Grid`）、BFS（`GridBFS`）、重み付きの最短路（`GridDijkstra`）、世代番号で管理する訪問済み配列（`Visited`） |
| `indexset.cpp` | 追加・削除・ランダムな取り出しが O(1) の整数の集合（`IndexSet`） |
| `zobrist.cpp` | Zobrist hash（`Zobrist`） |
| `fixedvector.cpp` | 容量を固定した vector（`FixedVector`） |
| `estimate.cpp` | 推定の部品。正規分布の確率（`normal_cdf` など）、平均と分散（`RunningStat`）、1次元のカルマンフィルタ（`GaussianEstimate`）、離散ベイズ推定（`DiscreteBayes`）、最小二乗法（`LeastSquares`） |
| `scripts/bundle.py` | `#include "..."` を展開して、提出用の1ファイルにまとめる |
| `scripts/compare.py` | pahcer の結果を seed ごとに比較する |
| `scripts/tune.py` | Optuna で pahcer を回し、パラメータを調整する |
| `scripts/plot_sa.py` | 焼きなましの推移ログをグラフにする |

## 準備

例ではコンテストのディレクトリをこのリポジトリと同じ階層に置いています。

```
ahc/
├── liblary/   このリポジトリ
└── ahcXXX/    コンテストのディレクトリ（pahcer_config.toml がある場所）
```

- C++20 でコンパイルします。`beamsearch.cpp` は concepts と `std::span` を使います。
- `template.cpp` は `#include <atcoder/all>` を含みます。手元でコンパイルするときは、AtCoder Library の場所を `-I` で指定するなどして、コンパイラが見つけられるようにしてください。
- スクリプトは Python 3 で動きます。`tune.py` には optuna、`plot_sa.py` には matplotlib が要ります。`compare.py` と `tune.py` は pahcer の結果を読みます。

## 使い方の流れ

1. `template.cpp` をコンテストのディレクトリに `main.cpp` としてコピーします。
2. 使うライブラリを `main.cpp` の template の部分より後ろで読み込みます。`annealing.cpp` は template の `timer` と `rnd` を使うので、template の部分より前には置けません。
   ```cpp
   #include "../liblary/grid.cpp"
   #include "../liblary/annealing.cpp"
   ```
3. `dump` を使うときは、pahcer_config.toml のコンパイルのオプションに `-DLOCAL` を足します。
   ```toml
   [[test.compile_steps]]
   program = "g++"
   args = ["-std=c++20", "-O2", "-DLOCAL", "main.cpp"]
   ```
4. 提出する前に、`bundle.py` で1ファイルにまとめます。
   ```bash
   python3 ../liblary/scripts/bundle.py main.cpp -o submit.cpp --check
   ```

## template.cpp

### 時間と乱数

- `timer`: プログラムの開始からの時間を測ります。`timer.setNowTime()` で今の時刻を取り込み、`timer.getNowTime()` でその時点までの経過時間[ms]を返します。`Time_Limit`（既定は 1900）を超えたかどうかは `timer.isTimeOver()` で分かります。
- `rnd`: xorshift の乱数です。`rnd.rand_int(l, r)` は [l, r) の整数、`rnd.rand_double()` は [0, 1) の小数を返します。

### パラメータの読み込み

```cpp
double T0 = param("T0", 1000.0);
int BEAM_WIDTH = param("BEAM_WIDTH", 300);
```

`param` は環境変数 `AHC_<名前>` があればその値を、なければ既定値を返します。提出時は環境変数がないので、既定値になります。`tune.py` はこの環境変数で値を渡します。読み方は既定値の型で決まるので、小数のパラメータは `1000.0` のように書いてください。

### デバッグ出力

```cpp
dump(x, v);  // [L42] x = 3, v = [1, 2, 3]
```

`-DLOCAL` を付けてコンパイルしたときだけ、変数名と値を stderr に出します。付けないときは何もせず、引数も評価しません。vector、map、set、pair、tuple とそれらの入れ子、`__int128`、配列は中身まで出せます。

## annealing.cpp

### 焼きなまし

```cpp
timer.setNowTime();
// T0, T1, 開始時刻[ms], 終了時刻[ms]
SimulatedAnnealing sa(param("T0", 1000.0), param("T1", 0.01), timer.getNowTime(), Time_Limit);
while (sa.next(score)) {
  int diff = ...;  // 新しいスコア - 今のスコア
  if (sa.accept(diff)) {
    // 採用
  } else {
    // 元に戻す
  }
}
```

- スコアを大きくする前提です。スコアを小さくする問題では、`diff` の正負を反転させて渡してください。
- 冷却は線形です。指数冷却にするときは、`next()` の中でコメントにしてある式に替えます。
- 時刻の取得と温度の更新は256回に1回だけ行います。採用の判定には `exp` を使わず、`log` の事前計算テーブルを使います。
- 反復回数は `sa.iter`、採用率は `(double)sa.accepted / sa.tried` で取れます。

### 推移ログとグラフ

環境変数 `AHC_SA_TRACE` にファイル名を入れて実行すると、時間・温度・採用率・スコアの推移を CSV に書きます。スコアも残すには、`sa.next()` の代わりに `sa.next(score)` を使います。環境変数がなければ何も書かないので、提出コードに残したままで構いません。

```bash
AHC_SA_TRACE=trace.csv ./a.out < tools/in/0000.txt > out.txt
python3 ../liblary/scripts/plot_sa.py trace.csv
```

`plot_sa.py` はスコアと最良スコア、温度、採用率を同じ時間軸で縦に並べて `trace.png` に描きます。多点スタートで何回も焼きなましたときは、区切りに縦線を引きます。出力先は `-o` で変えられます。最小化の問題で `-cost` を渡していたときは、`--negate` を付けると符号を戻して表示します。

### 近傍の選択

```cpp
NeighborSelector ns({1.0, 1.0, 0.3});  // 近傍 0, 1, 2 の初期の重み
int k = ns.select(rnd);
// 近傍 k を試す
ns.report(k, accepted, diff > 0);  // 採用したか、スコアが良くなったか
ns.print_stats({"swap", "move", "2-opt"});  // 近傍ごとの試行回数・採用率・改善率を stderr に出す
```

近傍の種類を重みに比例する確率で選びます。重みが 0 の近傍は選びません。第2引数を `true` にすると重みを自動で調整し、既定では1000回ごとに、その間の報酬（改善したら 1、改善しないが採用されたら 0.25、不採用なら 0）に重みを寄せます。

### 停滞の検出

```cpp
StagnationDetector stag(50000);  // 5万回、最良が更新されなければ停滞とみなす
if (stag.update(score)) {
  best = state;  // 最良を更新した
} else if (stag.stagnated()) {
  state = best;  // キック: 最良解に戻して大きく崩す、など
  kick(state);
  stag.reset();
}
```

スコアを大きくする前提です。スコアを小さくする問題では `-score` を渡してください。

### 多点スタート

`SimulatedAnnealing` は開始時刻と終了時刻を受け取るので、時間を区切って作り直せば多点スタートになります。例は `annealing.cpp` の先頭のコメントにあります。

## beamsearch.cpp

利用者が書くのは次の3つです。

- `Action`: 1手を表す型
- `Score`: 候補を比べる値。小さいほど良い候補として扱うので、最大化の問題では `-score` などに変えてください。
- `StateBase<Selector>`: `make_initial_node()`、`expand(parent, selector)`、`apply(action)`、`undo(action)` を持つ状態

```cpp
using Beam = etbs::EulerTourBeamSearch<Action, Score, StateBase>;
Beam::Config config;
config.max_turn = T;
config.beam_width = 1000;
StateBase<Beam::Selector> state;
StateBase<Beam::Selector> answer = Beam{}.beam_search(config, state);
```

`answer` は探索で選ばれた最良の経路を適用した最後の状態です。雛形と各設定の意味は `beamsearch.cpp` の先頭のコメントにあります。

### 重複除去

別々の手順で同じ状態にたどり着く問題では、`DedupEulerTourBeamSearch` を使います。`expand()` の中で `selector.push_with_hash(action, score, hash)` に子ノードの状態のハッシュ値を渡すと、同じターンに同じハッシュ値の候補が複数あれば最も良い1つだけを残します。ハッシュ値は `zobrist.cpp` の `Zobrist` を State に持たせ、`apply` と `undo` で差分更新して作るのが基本です。

```cpp
using Beam = etbs::DedupEulerTourBeamSearch<Action, Score, StateBase>;
```

### ビーム幅の自動調整

```cpp
timer.setNowTime();
config.beam_width = 100;                                 // 最初のターンの幅
config.time_limit_ms = Time_Limit - timer.getNowTime();  // この時間で全ターンを終える
```

ターンごとに処理時間を測り、「残り時間 ÷ 残りターン数」で回せるだけの幅に決め直します。`max_beam_width` で幅の上限を決められます。`width_log` に vector のアドレスを渡すと、各ターンの幅が記録されます。`time_limit_ms` を指定しなければ、幅は `beam_width` のまま変わりません。

## grid.cpp

### グリッド

```cpp
Grid grid(N, N);            // 4近傍
Grid grid8(N, N, 8);        // 8近傍
Grid torus(N, N, 4, true);  // 端がつながったグリッド
```

マス (r, c) を番号 `r * W + c` で扱います。`grid.next[v][d]` は方向 `d` に1マス進んだ先のマスで、盤面の外なら -1 です。方向の番号は template.cpp の `dx`、`dy`、`dir` と同じで、0:U 1:D 2:L 3:R です。8近傍ではさらに 4:右下 5:左下 6:右上 7:左上 を使います。逆方向は `Grid::reverse(d)` で求めます。

### BFS

通れるかどうかは入る先のマスで判定する関数か、移動で判定する関数のどちらかで渡します。

```cpp
GridBFS bfs(grid);
bfs.run(s, [&](int v) { return !obstacle[v]; });                  // マスの障害物
bfs.run(s, [&](int from, int to, int d) { return ...; });       // マスの間の壁
```

マスの障害物とマスの間の壁が両方ある問題では、移動で判定する関数の中で両方を見ます。

```cpp
// h[i][j] == '1': (i, j) と (i+1, j) の間に壁
// v[i][j] == '1': (i, j) と (i, j+1) の間に壁
auto can_move = [&](int from, int to, int d) {
  if (obstacle[to]) return false;  // マスの障害物
  int r = grid.row(from), c = grid.col(from);
  if (d == 0) return h[r - 1][c] == '0';  // U
  if (d == 1) return h[r][c] == '0';      // D
  if (d == 2) return v[r][c - 1] == '0';  // L
  return v[r][c] == '0';                  // R
};
bfs.run(s, can_move);
```

判定の関数が呼ばれるのは盤面の中への移動だけなので、0行目で `h[r - 1]` を読むことはありません。始点そのものは判定しません。

| 呼び出し | 内容 |
|---|---|
| `bfs.run(s, can, t)` | `t` に着いた時点で打ち切る |
| `bfs.run(s, can, -1, 5)` | 距離 5 までだけ調べる |
| `bfs.run(std::vector<int>{a, b}, can)` | 複数始点。各マスには一番近い始点からの距離が入る |
| `bfs.dist(v)` | 距離。届かなければ -1 |
| `bfs.source(v)` | `v` に一番近い始点。届かなければ -1 |
| `bfs.path_dirs(t)` | 始点から `t` までの方向の列 |
| `bfs.path(t)` | 始点から `t` までのマスの列 |
| `bfs.order()` | 届いたマスを近い順に並べたもの |

`GridBFS` は配列を使い回すので、何度呼んでも初期化のコストがかかりません。訪問済みの管理に使っている `Visited` は、`reset()` が O(1) の配列として単独でも使えます。

### 重み付きの最短路

移動にコストがかかる問題では `GridDijkstra` を使います。使い方は `GridBFS` と同じで、通れるかの判定の代わりにコストを返す関数を渡します。

```cpp
GridDijkstra dij(grid);
dij.run(s, [&](int v) { return wall[v] ? -1 : cost[v]; });                // マスに入るコスト
dij.run01(s, [&](int from, int to, int d) { return road[to] ? 0 : 1; });  // コストが 0 か 1 だけのとき
```

- コストは「入る先のマス」で返す関数か、「移動」で返す関数のどちらかで渡します。負の値を返した移動は通れません。始点のマスのコストは数えません。
- `run()` は Dijkstra です。コストが 0 か 1 だけなら、Dijkstra より速い 0-1 BFS の `run01()` を使えます。
- 複数始点、`goal` と `max_dist` での打ち切り、`dist`、`source`、`path_dirs`、`path`、`order` は `GridBFS` と同じです。距離は `long long` で、届かなければ -1 です。
- `goal` や `max_dist` で打ち切ったときは、そこまでに距離が確定したマスだけを届いたマスとして扱います。

## indexset.cpp

```cpp
IndexSet st(N);
st.insert(x);
st.erase(x);
int y = st.random(rnd);  // 空でないこと
for (int z : st) { ... }  // 順番は不定
```

0 以上 N 未満の整数の集合です。追加、削除、存在判定、ランダムな取り出しがすべて O(1) です。削除すると末尾の要素が空いた位置へ移るので、列挙の途中で追加や削除をしないでください。

## zobrist.cpp

```cpp
Zobrist z(N * N, C);  // 位置の数, 値の種類数
hash ^= z(i, board[i]) ^ z(i, c);  // board[i] を c に書き換えるとき
board[i] = c;
```

「位置 i に値 v がある」ことに 64bit の乱数を割り当て、状態のハッシュ値をそれらの XOR で表します。1か所を書き換えるたびに O(1) で差分更新できます。

## fixedvector.cpp

```cpp
FixedVector<int, 100> v;
v.push_back(3);
v.swap_remove(i);  // O(1)。順番は変わる
v.erase(i);        // O(size)。順番は保つ
```

要素を自分の中に持ち、ヒープ確保をしない vector です。コピーは使っている要素の分だけで済みます。容量を超えると、`-DNDEBUG` を付けていなければ assert で止まります。

## estimate.cpp

ノイズのある観測から隠れた値を推定する問題で使う部品です。

### 正規分布

| 関数 | 返す値 |
|---|---|
| `normal_pdf(x, mu, sigma)` | 確率密度 |
| `normal_logpdf(x, mu, sigma)` | 確率密度の対数 |
| `normal_cdf(x, mu, sigma)` | P(X <= x) |
| `normal_logcdf(x, mu, sigma)` | log P(X <= x)。`normal_cdf` が 0 になる裾でも使える |
| `normal_prob_between(lo, hi, mu, sigma)` | P(lo < X < hi)。`lo` と `hi` には ±infinity も使える |
| `normal_quantile(p, mu, sigma)` | P(X <= x) = p となる x |

`mu` と `sigma` を省くと標準正規分布になります。`normal_prob_between` は裾どうしの差でも桁落ちしにくく、丸めた観測の尤度などに使えます。`normal_quantile` は二分法で求めるので、1回に `erfc` を100回ほど呼びます。

### 平均と分散

```cpp
RunningStat rs;
rs.add(x);
rs.mean;        // 平均
rs.variance();  // 不偏分散
```

値を1つずつ足しながら、平均と分散を求めます（Welford 法）。

### カルマンフィルタ

```cpp
GaussianEstimate est(500, 100 * 100);  // 事前の平均と分散
est.observe(y, 30 * 30);               // 観測 y = 値 + ノイズ（ノイズの分散 900）
est.drift(process_var);                // 値が時間とともにランダムに動くとき、観測の間に分散を足す
```

未知の値の推定を正規分布で持ち、観測のたびに平均 `est.mean` と分散 `est.var` を更新します（1次元のカルマンフィルタ）。

### 離散ベイズ推定

```cpp
DiscreteBayes bayes(K);  // 候補 0..K-1、事前分布は一様
bayes.update([&](int k) { return normal_logpdf(y, value[k], sigma); });  // 観測 y の対数尤度
int best = bayes.argmax();                                 // 事後確率が最大の候補
std::vector<double> p = bayes.probs();                     // 事後確率
double e = bayes.expect([&](int k) { return value[k]; });  // 期待値
```

候補が有限個のとき、候補ごとに対数尤度を足して事後確率を求めます。ありえない候補には -infinity を返します。足すたびに最大値が 0 になるようにずらすので、何回足しても桁があふれません。事前分布は `DiscreteBayes(std::vector<double>{...})` のように確率で渡せます。

### 最小二乗法

```cpp
LeastSquares ls(2);
ls.add({1.0, x}, y);                 // 定数項を入れるなら 1 を置く
std::vector<double> w = ls.solve();  // y ≈ w[0] + w[1] * x
```

`add(x, y, weight)` で重みを付けられ、`solve(ridge)` でリッジ正則化の強さを指定できます（既定は 1e-9）。データが足りず解が決まらない変数は 0 になります。説明変数の数は数十までを想定しています。

## スクリプト

どれもコンテストのディレクトリで実行します。

### bundle.py

```bash
python3 ../liblary/scripts/bundle.py main.cpp -o submit.cpp --check
```

展開するのは `#include "..."` だけで、`<bits/stdc++.h>` や `<atcoder/all>` はそのまま残します。同じファイルは1回だけ展開し、ブロックコメントの中の `#include` は展開しません。`#pragma once` は消します。`-o` を省くと標準出力に書きます。`--check` を付けると、まとめたコードを g++ でコンパイルできるか確かめます。g++ のオプションは `--cxx-flags` で変えられます。

### compare.py

```bash
python3 ../liblary/scripts/compare.py                    # 最新の結果と、その1つ前を比較
python3 ../liblary/scripts/compare.py -b best            # 最新の結果と、seed ごとのベストを比較
python3 ../liblary/scripts/compare.py -b -3 -t -1        # 3つ前と最新を比較
python3 ../liblary/scripts/compare.py --params N,M,_,T   # 入力1行目の値ごとに集計
```

平均スコア、seed ごとの比の幾何平均、勝ち・負け・引き分けの数、悪くなった seed と良くなった seed の上位を表示します。比は改善した向きが + です。エラーになった seed は比較から除きます。`-b` と `-t` にはファイル名の一部、タグ名、コメントも指定できます。`--params` には入力1行目の各値の名前を並べ、使わない列は `_` にします。

### tune.py

```bash
python3 ../liblary/scripts/tune.py -p T0:float:10:10000:log -p T1:float:0.001:10:log --init T0=1000,T1=0.01
```

C++ 側では `param()` で値を読みます。パラメータは `名前:型:下限:上限[:log]`（型は `int` か `float`）か、`名前:cat:値1,値2` の形で渡します。

- seed の順番をシャッフルして1ケースずつ結果を受け取り、ベストより明らかに悪い試行は途中で打ち切ります（Optuna の WilcoxonPruner）。
- 評価値は既定ではスコアの対数の平均（幾何平均）です。`--metric raw` を付けるとスコアの平均になります。
- エラーが出た試行と、実行時間が `--time-limit` の秒数を超えたケースがある試行は、失敗として扱います。
- 結果は `tune.db` に保存されるので、同じコマンドで続きから再開できます。
- pahcer を `--freeze-best-scores` と `--no-result-file` を付けて実行するので、pahcer のベストと履歴は変わりません。
- 最後に、C++ の既定値を書き換えるための `param(...)` の行を表示します。

### plot_sa.py

焼きなましの推移ログをグラフにします。使い方は「推移ログとグラフ」にあります。
