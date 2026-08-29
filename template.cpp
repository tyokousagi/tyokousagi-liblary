// I love hina-no.
// https://github.com/tyokousagi/tyokousagi-liblary

#pragma GCC optimize("O3")
#pragma GCC optimize("unroll-loops")
#include <bits/stdc++.h>

#include <atcoder/all>
using namespace atcoder;
#include <unordered_set>

using namespace std;  // std:: を省略

using ll = long long;
using ld = long double;
using i128 = __int128_t;
using ull = unsigned long long;
using P = pair<int, int>;

#define rep(i, n) for (int i = 0; i < int(n); ++i)
#define REP(i, x, n) for (int i = x; i < int(n); ++i)
#define DREP(i, r, l) for (int i = (int)(r) - 1; i >= (int)(l); --i)
#define drep(i, n) DREP(i, n, 0)
#define all(x) begin(x), end(x)
#define rall(x) rbegin(x), rend(x)
#define MAX *max_element
#define MIN *min_element
#define pb push_back
#define pf push_front
#define ppb pop_back
#define ppf pop_front
#define eb emplace_back
#define fi first
#define se second
#define p_queue priority_queue
#define pcnt __builtin_popcountll

template <class T>
inline bool chmax(T &a, T b) {
  return a < b ? a = b, 1 : 0;
}
template <class T>
inline bool chmin(T &a, T b) {
  return a > b ? a = b, 1 : 0;
}

const int INF = 1e9;

const int dx[] = {-1, 1, 0, 0, 1, 1, -1, -1};
const int dy[] = {0, 0, -1, 1, 1, -1, 1, -1};
const char dir[4] = {'U', 'D', 'L', 'R'};
int r_dir[256];

// 時間を管理するクラス
class TimeKeeper {
  std::chrono::high_resolution_clock::time_point start_time_;
  int64_t time_threshold_;
  double now_time_ = 0;

 public:
  TimeKeeper(const int64_t &time_threshold)
      : start_time_(std::chrono::high_resolution_clock::now()), time_threshold_(time_threshold) {
  }
  void setNowTime() {
    auto diff = std::chrono::high_resolution_clock::now() - start_time_;
    now_time_ = std::chrono::duration_cast<std::chrono::microseconds>(diff).count() * 1e-3;
  }
  bool isTimeOver() const {
    auto diff = std::chrono::high_resolution_clock::now() - start_time_;
    return std::chrono::duration_cast<std::chrono::milliseconds>(diff).count() >= time_threshold_;
  }
  double getNowTime() const {
    return now_time_;
  }
};

struct Random {
  using ull = unsigned long long;
  ull x;

  Random(ull seed = 0) {
    if (seed == 0) {
      seed = std::chrono::steady_clock::now().time_since_epoch().count();
    }
    x = seed | 1;
  }

  // 64bit 乱数
  inline ull nextU64() {
    ull y = x;
    y ^= y << 7;
    y ^= y >> 9;
    y ^= y << 8;
    x = y;
    return y;
  }

  // [l, r)
  inline int rand_int(int l, int r) {
    return l + (int)(nextU64() % (ull)(r - l));
  }

  // [0, 1)
  inline double rand_double() {
    return (nextU64() >> 11) * (1.0 / (1ULL << 53));
  }

  // log(U)
  inline double next_log() {
    return std::log(rand_double());
  }
} rnd(11110814);

int Time_Limit = 1900;
auto timer = TimeKeeper(Time_Limit);

bool sa_accept(int diff) {
  // スコアを小さくする場合はdiffの正負を反転させる
  // diff = -diff;
  timer.setNowTime();
  double T0 = 1000;
  double T1 = 0.01;
  double e_ms = timer.getNowTime();
  double progress = min(1.0, max(0.0, e_ms / Time_Limit));
  // 線形冷却
  double temp = T0 + (T1 - T0) * progress;
  // 指数冷却
  // double temp = T0 * pow(T1 / T0, progress);
  if (rnd.rand_double() < exp(double(diff) / temp)) {
    return true;
  } else {
    return false;
  }
}

int main() {
  ios::sync_with_stdio(false);
  cin.tie(0);
  cout.tie(0);
  return 0;
}