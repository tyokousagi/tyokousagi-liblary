// I love hina-no.
// tyokousagi-焼きなましライブラリ
//
// - SimulatedAnnealing : 温度の管理と採用判定（高速版）
// - NeighborSelector   : 近傍の種類を重み付きで選ぶ。採用・改善の実績から重みを自動で調整することもできる
// - StagnationDetector : 最良スコアがしばらく更新されていないことを検出する（キックの合図）
//
// template.cpp の後に include して使う（template.cpp の timer と rnd を使う）。
//
// ============================================================
// 使い方の例
// ============================================================
//
//   NeighborSelector ns({1.0, 1.0, 0.3});  // 近傍 0, 1, 2 の初期の重み。第2引数を true にすると重みを自動調整する
//   StagnationDetector stag(50000);        // 5万回、最良が更新されなければ停滞とみなす
//   State best = state;
//
//   timer.setNowTime();
//   SimulatedAnnealing sa(param("T0", 1000.0), param("T1", 0.01), timer.getNowTime(), Time_Limit);
//   while (sa.next(state.score)) {
//     int k = ns.select(rnd);
//     int diff = try_neighbor(k);  // 近傍 k を試す（スコアの増分）
//     bool ok = sa.accept(diff);
//     if (ok) {
//       // 採用
//     } else {
//       // 元に戻す
//     }
//     ns.report(k, ok, diff > 0);
//
//     if (stag.update(state.score)) {
//       best = state;  // 最良を更新した
//     } else if (stag.stagnated()) {
//       // キック: 最良解に戻して大きく崩す、など
//       state = best;
//       kick(state);
//       stag.reset();
//     }
//   }
//   ns.print_stats({"swap", "move", "2-opt"});  // 近傍ごとの試行回数・採用率・改善率を cerr に出す
//
// ============================================================
// 多点スタートの例
// ============================================================
//
// 時間を K 等分して、それぞれ別の初期解から焼きなまし、最良を残す。
// SimulatedAnnealing は開始・終了時刻を受け取るので、区間ごとに作り直すだけでよい。
//
//   timer.setNowTime();
//   double start = timer.getNowTime();
//   for (int s = 0; s < K; ++s) {
//     State state = make_initial_solution();
//     SimulatedAnnealing sa(T0, T1, start + (Time_Limit - start) * s / K, start + (Time_Limit - start) * (s + 1) / K);
//     while (sa.next()) { ... }
//     if (state.score > best.score) best = state;
//   }
//
// 「前半は複数の初期解を短く回し、後半は一番良かったものだけを続ける」も同じ要領で書ける。

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <vector>

// 焼きなまし
// - 時刻の取得と温度の更新は UPDATE_INTERVAL 回に1回だけ行う
// - exp(diff / temp) > U  ⇔  diff > temp * log(U) なので、exp の代わりに log(U) の事前計算テーブルで判定する
// スコアを大きくする前提。スコアを小さくする場合は diff の正負を反転させて渡す
//
// 使い方:
//   timer.setNowTime();
//   // T0, T1, 開始時刻[ms], 終了時刻[ms]
//   SimulatedAnnealing sa(param("T0", 1000.0), param("T1", 0.01), timer.getNowTime(), Time_Limit);
//   while (sa.next(score)) {  // 推移ログにスコアを残さないなら sa.next() でもよい
//     int diff = ...;         // 新しいスコア - 今のスコア
//     if (sa.accept(diff)) {
//       // 採用
//     } else {
//       // 元に戻す
//     }
//   }
//   cerr << "iter = " << sa.iter << ", 採用率 = " << (double)sa.accepted / sa.tried << endl;
//
// 推移ログ:
//   環境変数 AHC_SA_TRACE にファイル名を入れて実行すると、時間・温度・採用率・スコアの推移を CSV に書く。
//   環境変数がなければ何もしないので、提出コードに残したままでよい。
//   1回の焼きなましにつき TRACE_POINTS 行ほど。多点スタートで何回も焼きなますと run 列で区別される。
//     AHC_SA_TRACE=trace.csv ./a.out < tools/in/0000.txt > out.txt
//     python3 scripts/plot_sa.py trace.csv  # trace.png にグラフを描く
struct SimulatedAnnealing {
  static constexpr int UPDATE_INTERVAL = 256;  // 2のべき乗
  static constexpr int LOG_TABLE_SIZE = 1 << 16;
  static constexpr int TRACE_POINTS = 500;
  static inline double log_table[LOG_TABLE_SIZE];  // log(U)、U は (0, 1) の一様乱数
  static inline bool log_table_ready = false;
  static inline int instance_count = 0;  // 推移ログで何回目の焼きなましかを区別する

  double T0, T1;
  double start_ms, end_ms;
  double temp;
  double progress = 0;  // 0 → 1。近傍の選び方を時間で変えたいときにも使える
  long long iter = 0;
  long long tried = 0, accepted = 0;  // accept() を呼んだ回数と、そのうち採用した回数

  SimulatedAnnealing(double T0, double T1, double start_ms, double end_ms)
      : T0(T0), T1(T1), start_ms(start_ms), end_ms(end_ms), temp(T0) {
    if (!log_table_ready) {
      for (int i = 0; i < LOG_TABLE_SIZE; ++i) {
        log_table[i] = std::log((i + 0.5) / LOG_TABLE_SIZE);
      }
      log_table_ready = true;
    }
    run_id_ = instance_count++;
    if (const char *path = std::getenv("AHC_SA_TRACE")) {
      trace_.reset(std::fopen(path, run_id_ == 0 ? "w" : "a"));
      if (trace_ && run_id_ == 0) {
        std::fprintf(trace_.get(), "run,time_ms,iter,temp,accept_rate,score,best\n");
      }
    }
  }

  // 次の反復へ進む。時間切れなら false
  bool next() {
    if ((iter++ & (UPDATE_INTERVAL - 1)) == 0) {
      timer.setNowTime();
      double now_ms = timer.getNowTime();
      progress = (now_ms - start_ms) / (end_ms - start_ms);
      if (progress >= 1.0) {
        if (trace_) {
          write_trace(now_ms);
        }
        return false;
      }
      // 線形冷却
      temp = T0 + (T1 - T0) * progress;
      // 指数冷却
      // temp = T0 * std::pow(T1 / T0, progress);
      if (trace_ && now_ms >= next_trace_ms_) {
        write_trace(now_ms);
      }
    }
    return true;
  }

  // next() と同じ。今のスコアを推移ログに残す
  bool next(double score) {
    score_ = score;
    best_ = std::max(best_, score);
    return next();
  }

  // diff >= 0 なら必ず採用、diff < 0 なら確率 exp(diff / temp) で採用
  bool accept(double diff) {
    ++tried;
    if (diff >= 0) {
      ++accepted;
      return true;
    }
    bool ok = diff > temp * log_table[rnd.nextU64() & (LOG_TABLE_SIZE - 1)];
    accepted += ok;
    return ok;
  }

 private:
  struct FileCloser {
    void operator()(std::FILE *f) const {
      std::fclose(f);
    }
  };
  std::unique_ptr<std::FILE, FileCloser> trace_;
  int run_id_ = 0;
  double next_trace_ms_ = 0;
  long long last_tried_ = 0, last_accepted_ = 0;
  double score_ = std::numeric_limits<double>::quiet_NaN();
  double best_ = -std::numeric_limits<double>::infinity();

  // 1行書く。採用率は前の行からの区間のもの
  void write_trace(double now_ms) {
    long long window_tried = tried - last_tried_, window_accepted = accepted - last_accepted_;
    last_tried_ = tried;
    last_accepted_ = accepted;
    std::fprintf(trace_.get(), "%d,%.3f,%lld,%.6g,", run_id_, now_ms, iter, temp);
    if (window_tried > 0) {
      std::fprintf(trace_.get(), "%.6f", (double)window_accepted / window_tried);
    }
    if (std::isnan(score_)) {
      std::fprintf(trace_.get(), ",,\n");
    } else {
      std::fprintf(trace_.get(), ",%.17g,%.17g\n", score_, best_);
    }
    next_trace_ms_ = now_ms + (end_ms - start_ms) / TRACE_POINTS;
  }
};

struct NeighborSelector {
  int K;
  std::vector<double> weight;                        // 選ばれる確率（合計 1）
  std::vector<long long> tried, accepted, improved;  // 近傍ごとの累計

  // 重みの自動調整（adaptive = true のとき）
  // segment 回ごとに、その区間での「1回あたりの報酬」に重みを近づける。
  // 報酬は 改善した = 1、改善はしないが採用された = 0.25、不採用 = 0。改善の大きさは見ない。
  bool adaptive;
  int segment = 1000;     // 重みを更新する間隔
  double reaction = 0.2;  // 1回の更新でどれだけ実績に寄せるか（0〜1）
  double min_share = 0.2;  // 重みの下限（均等に割ったときの何倍か）。全く選ばれなくなるのを防ぐ

  NeighborSelector(const std::vector<double> &initial_weight, bool adaptive = false)
      : K((int)initial_weight.size()),
        weight(initial_weight),
        tried(K),
        accepted(K),
        improved(K),
        adaptive(adaptive),
        enabled_(K),
        seg_tried_(K),
        seg_reward_(K) {
    assert(K > 0);
    for (int k = 0; k < K; ++k) {
      assert(initial_weight[k] >= 0);
      enabled_[k] = initial_weight[k] > 0;
    }
    normalize();
  }

  template <class Rng>
  int select(Rng &rng) const {
    double r = rng.rand_double();
    for (int k = 0; k < K; ++k) {
      if (r < weight[k]) {
        return k;
      }
      r -= weight[k];
    }
    return last_enabled_;  // 浮動小数点の誤差で r が余ったとき
  }

  // 近傍 k を試した結果を伝える。is_accepted: 採用したか、is_improved: スコアが良くなったか
  void report(int k, bool is_accepted, bool is_improved) {
    ++tried[k];
    accepted[k] += is_accepted;
    improved[k] += is_improved;
    if (!adaptive) {
      return;
    }
    ++seg_tried_[k];
    seg_reward_[k] += is_improved ? 1.0 : is_accepted ? 0.25 : 0.0;
    if (++seg_count_ >= segment) {
      update_weight();
    }
  }

  // 近傍ごとの統計を cerr に出す
  void print_stats(const std::vector<std::string> &names = {}) const {
    std::fprintf(stderr, "%-12s %7s %12s %8s %8s\n", "neighbor", "weight", "tried", "accept", "improve");
    for (int k = 0; k < K; ++k) {
      std::string name = k < (int)names.size() ? names[k] : std::to_string(k);
      double t = std::max<long long>(tried[k], 1);
      std::fprintf(stderr, "%-12s %6.1f%% %12lld %7.2f%% %7.2f%%\n", name.c_str(), weight[k] * 100, tried[k],
                   accepted[k] / t * 100, improved[k] / t * 100);
    }
  }

 private:
  std::vector<bool> enabled_;  // 初期の重みが 0 の近傍は選ばない
  std::vector<long long> seg_tried_;
  std::vector<double> seg_reward_;
  int seg_count_ = 0;
  int last_enabled_ = 0;

  void normalize() {
    double sum = 0;
    for (int k = 0; k < K; ++k) {
      sum += weight[k];
    }
    assert(sum > 0);
    for (int k = 0; k < K; ++k) {
      weight[k] /= sum;
      if (enabled_[k]) {
        last_enabled_ = k;
      }
    }
  }

  void update_weight() {
    // この区間で試された近傍の間で、1回あたりの報酬を合計 1 に正規化し、今の重みに混ぜる。
    // 試されなかった近傍の重みはそのまま。
    double rate_sum = 0, weight_sum = 0;
    for (int k = 0; k < K; ++k) {
      if (seg_tried_[k] > 0) {
        rate_sum += seg_reward_[k] / seg_tried_[k];
        weight_sum += weight[k];
      }
    }
    if (rate_sum > 0) {
      for (int k = 0; k < K; ++k) {
        if (seg_tried_[k] > 0) {
          double target = weight_sum * (seg_reward_[k] / seg_tried_[k]) / rate_sum;
          weight[k] = (1 - reaction) * weight[k] + reaction * target;
        }
      }
      int enabled_count = (int)std::count(enabled_.begin(), enabled_.end(), true);
      for (int k = 0; k < K; ++k) {
        if (enabled_[k]) {
          weight[k] = std::max(weight[k], min_share / enabled_count);
        }
      }
      normalize();
    }
    std::fill(seg_tried_.begin(), seg_tried_.end(), 0);
    std::fill(seg_reward_.begin(), seg_reward_.end(), 0.0);
    seg_count_ = 0;
  }
};

// 最良スコアがしばらく更新されていないことを検出する。
// スコアを大きくする前提。スコアを小さくする場合は -score を渡す。
struct StagnationDetector {
  long long patience;   // この回数、最良が更新されなければ停滞とみなす
  long long since = 0;  // 最後に最良を更新してからの回数
  double best = -std::numeric_limits<double>::infinity();

  explicit StagnationDetector(long long patience) : patience(patience) {
  }

  // 今のスコアを伝える。最良を更新したら true
  bool update(double score) {
    if (score > best) {
      best = score;
      since = 0;
      return true;
    }
    ++since;
    return false;
  }

  bool stagnated() const {
    return since >= patience;
  }

  // キックした後などに、停滞のカウントだけをやり直す（最良スコアは覚えたまま）
  void reset() {
    since = 0;
  }
};
