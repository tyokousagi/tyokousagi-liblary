// I love hina-no.
// tyokousagi-推定ライブラリ
//
// ノイズのある観測から隠れた値を推定する問題で使う部品。
// - 正規分布         : normal_pdf / normal_logpdf / normal_cdf / normal_logcdf / normal_prob_between / normal_quantile
// - RunningStat      : 値を1つずつ足しながら平均と分散を求める（Welford 法）
// - GaussianEstimate : 未知の値の推定を正規分布で持ち、観測のたびに更新する（1次元のカルマンフィルタ）
// - DiscreteBayes    : 候補が有限個のときのベイズ推定。候補ごとに対数尤度を足していく
// - LeastSquares     : 最小二乗法。データを1つずつ足し、重みとリッジ正則化も使える
//
// 使い方:
//   normal_cdf(x, mu, sigma);                  // P(X <= x)、X ~ N(mu, sigma^2)
//   normal_prob_between(lo, hi, mu, sigma);    // P(lo < X < hi)。丸めた観測の尤度などに使う
//   normal_logcdf(x, mu, sigma);               // log P(X <= x)。確率が小さすぎて 0 になる所でも使える
//
//   GaussianEstimate est(500, 100 * 100);      // 事前の平均と分散
//   est.observe(y, 30 * 30);                   // 観測 y = 値 + ノイズ（ノイズの分散 900）
//
//   DiscreteBayes bayes(K);                    // 候補 0..K-1、事前分布は一様
//   bayes.update([&](int k) { return normal_logpdf(y, value[k], sigma); });
//   int best = bayes.argmax();
//
//   LeastSquares ls(2);
//   ls.add({1.0, x}, y);                       // 定数項を入れるなら 1 を置く
//   std::vector<double> w = ls.solve();        // y ≈ w[0] + w[1] * x

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

// ============================================================
// 正規分布 N(mu, sigma^2)
// ============================================================

inline double normal_pdf(double x, double mu = 0, double sigma = 1) {
  double z = (x - mu) / sigma;
  return std::exp(-0.5 * z * z) / (sigma * std::sqrt(2 * std::numbers::pi));
}

inline double normal_logpdf(double x, double mu = 0, double sigma = 1) {
  double z = (x - mu) / sigma;
  return -0.5 * z * z - std::log(sigma) - 0.5 * std::log(2 * std::numbers::pi);
}

// P(X <= x)
inline double normal_cdf(double x, double mu = 0, double sigma = 1) {
  return 0.5 * std::erfc(-(x - mu) / (sigma * std::numbers::sqrt2));
}

// log P(X <= x)。x が平均よりずっと小さく、normal_cdf が 0 になる所でも使える
inline double normal_logcdf(double x, double mu = 0, double sigma = 1) {
  double z = (x - mu) / sigma;
  if (z > -30) {
    return std::log(0.5 * std::erfc(-z / std::numbers::sqrt2));
  }
  // 裾の漸近展開: Φ(z) ≈ φ(z) / (-z) * (1 - 1/z^2 + 3/z^4 - 15/z^6)
  double z2 = z * z;
  return -0.5 * z2 - std::log(-z) - 0.5 * std::log(2 * std::numbers::pi) +
         std::log1p(-1 / z2 + 3 / (z2 * z2) - 15 / (z2 * z2 * z2));
}

// P(lo < X < hi)。lo, hi には ±infinity も使える。裾どうしの差でも桁落ちしにくい
inline double normal_prob_between(double lo, double hi, double mu = 0, double sigma = 1) {
  if (lo >= hi) {
    return 0;
  }
  double a = (lo - mu) / (sigma * std::numbers::sqrt2);
  double b = (hi - mu) / (sigma * std::numbers::sqrt2);
  if (a >= 0) {
    return 0.5 * (std::erfc(a) - std::erfc(b));  // 両方とも平均より右
  }
  if (b <= 0) {
    return 0.5 * (std::erfc(-b) - std::erfc(-a));  // 両方とも平均より左
  }
  return 1 - 0.5 * (std::erfc(-a) + std::erfc(b));  // 平均をまたぐ
}

// P(X <= x) = p となる x（0 < p < 1）。二分法で求めるので、erfc を100回ほど呼ぶ
inline double normal_quantile(double p, double mu = 0, double sigma = 1) {
  assert(0 < p && p < 1);
  if (p > 0.5) {
    return mu - sigma * normal_quantile(1 - p);  // 上の裾は対称にして精度を保つ
  }
  double lo = -40, hi = 0;
  for (int i = 0; i < 100; ++i) {
    double mid = (lo + hi) / 2;
    if (mid == lo || mid == hi) {
      break;
    }
    if (normal_cdf(mid) < p) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  return mu + sigma * (lo + hi) / 2;
}

// ============================================================
// RunningStat: 値を1つずつ足しながら平均と分散を求める
// ============================================================
struct RunningStat {
  long long n = 0;
  double mean = 0;
  double m2 = 0;  // 平均からのずれの2乗和

  void add(double x) {
    ++n;
    double d = x - mean;
    mean += d / n;
    m2 += d * (x - mean);
  }

  // 不偏分散（n - 1 で割る）。2個未満なら 0
  double variance() const {
    return n > 1 ? m2 / (n - 1) : 0;
  }

  double stddev() const {
    return std::sqrt(variance());
  }
};

// ============================================================
// GaussianEstimate: 未知の値 x の推定を N(mean, var) で持ち、観測のたびに更新する
// ============================================================
// 観測は y = x + ノイズ（ノイズは平均 0、分散 noise_var の正規分布）を想定する。
// x が時間とともにランダムに動くなら、観測と観測の間に drift() で分散を足す。
struct GaussianEstimate {
  double mean, var;

  GaussianEstimate(double mean, double var) : mean(mean), var(var) {
  }

  void observe(double y, double noise_var) {
    double k = var / (var + noise_var);
    mean += k * (y - mean);
    var *= 1 - k;
  }

  void drift(double process_var) {
    var += process_var;
  }

  double stddev() const {
    return std::sqrt(var);
  }
};

// ============================================================
// DiscreteBayes: 候補が有限個のときのベイズ推定
// ============================================================
// 各候補の対数事後確率を持ち、観測のたびに対数尤度 log P(観測 | 候補 k) を足す。
// 足したあと最大値が 0 になるようにずらすので、何回足しても桁があふれない。
struct DiscreteBayes {
  std::vector<double> logp;

  // 候補 0..K-1、事前分布は一様
  explicit DiscreteBayes(int K) : logp(K, 0.0) {
  }

  // 事前分布を確率で渡す（合計は 1 でなくてよい）
  explicit DiscreteBayes(const std::vector<double> &prior) : logp(prior.size()) {
    for (int k = 0; k < (int)prior.size(); ++k) {
      logp[k] = std::log(prior[k]);
    }
    shift();
  }

  int size() const {
    return (int)logp.size();
  }

  // loglik(k) = log P(観測 | 候補 k)。ありえない候補には -infinity を返す
  template <class F>
  void update(F loglik) {
    for (int k = 0; k < size(); ++k) {
      logp[k] += loglik(k);
    }
    shift();
  }

  // 事後確率（合計 1）
  std::vector<double> probs() const {
    std::vector<double> p(size());
    double sum = 0;
    for (int k = 0; k < size(); ++k) {
      p[k] = std::exp(logp[k]);
      sum += p[k];
    }
    assert(sum > 0);  // すべての候補がありえないと 0 になる
    for (double &x : p) {
      x /= sum;
    }
    return p;
  }

  // 事後確率が最大の候補
  int argmax() const {
    return (int)(std::max_element(logp.begin(), logp.end()) - logp.begin());
  }

  // 事後分布での f(k) の期待値
  template <class F>
  double expect(F f) const {
    std::vector<double> p = probs();
    double e = 0;
    for (int k = 0; k < size(); ++k) {
      if (p[k] > 0) {
        e += p[k] * f(k);
      }
    }
    return e;
  }

 private:
  void shift() {
    double mx = *std::max_element(logp.begin(), logp.end());
    if (mx == -std::numeric_limits<double>::infinity()) {
      return;
    }
    for (double &x : logp) {
      x -= mx;
    }
  }
};

// ============================================================
// LeastSquares: 最小二乗法
// ============================================================
// y ≈ w[0] * x[0] + ... + w[D-1] * x[D-1] となる w を求める。定数項を入れるなら x のどれかを 1 にする。
// (X^T W X + ridge * I) w = X^T W y をガウスの消去法で解く。D は数十までを想定。
struct LeastSquares {
  int D;
  std::vector<double> xtx;  // X^T W X（D × D）
  std::vector<double> xty;  // X^T W y
  long long n = 0;

  explicit LeastSquares(int D) : D(D), xtx(D * D, 0.0), xty(D, 0.0) {
  }

  // データを1つ足す。weight はそのデータの重み
  void add(const std::vector<double> &x, double y, double weight = 1) {
    assert((int)x.size() == D);
    for (int i = 0; i < D; ++i) {
      double wx = weight * x[i];
      for (int j = 0; j < D; ++j) {
        xtx[i * D + j] += wx * x[j];
      }
      xty[i] += wx * y;
    }
    ++n;
  }

  // ridge はリッジ正則化の強さ。データが足りず解が決まらない変数は 0 になる
  std::vector<double> solve(double ridge = 1e-9) const {
    std::vector<std::vector<double>> a(D, std::vector<double>(D + 1));
    for (int i = 0; i < D; ++i) {
      for (int j = 0; j < D; ++j) {
        a[i][j] = xtx[i * D + j] + (i == j ? ridge : 0);
      }
      a[i][D] = xty[i];
    }
    std::vector<int> pivot_col;
    int row = 0;
    for (int col = 0; col < D && row < D; ++col) {
      int best = row;
      for (int i = row + 1; i < D; ++i) {
        if (std::abs(a[i][col]) > std::abs(a[best][col])) {
          best = i;
        }
      }
      if (std::abs(a[best][col]) < 1e-12) {
        continue;
      }
      std::swap(a[row], a[best]);
      for (int i = 0; i < D; ++i) {
        if (i == row || a[i][col] == 0) {
          continue;
        }
        double f = a[i][col] / a[row][col];
        for (int j = col; j <= D; ++j) {
          a[i][j] -= f * a[row][j];
        }
      }
      pivot_col.push_back(col);
      ++row;
    }
    std::vector<double> w(D, 0.0);
    for (int r = 0; r < (int)pivot_col.size(); ++r) {
      int col = pivot_col[r];
      w[col] = a[r][D] / a[r][col];
    }
    return w;
  }

  // w で x から y を予測する
  static double predict(const std::vector<double> &w, const std::vector<double> &x) {
    double s = 0;
    for (int i = 0; i < (int)w.size(); ++i) {
      s += w[i] * x[i];
    }
    return s;
  }
};
