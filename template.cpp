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

// デバッグ出力。LOCAL が定義されているとき（g++ -DLOCAL）だけ「変数名 = 値」を stderr に出す。提出時は何もしない
// vector / pair / tuple / map / set や、それらの入れ子も中身まで出せる
//   dump(x, v);  // [L42] x = 3, v = [1, 2, 3]
#ifdef LOCAL
template <class T>
void debug_out(const T &x) {
  if constexpr (is_convertible_v<const T &, string_view>) {
    cerr << '"' << x << '"';
  } else if constexpr (is_same_v<T, i128>) {
    i128 v = x;
    if (v < 0) {
      cerr << '-';
      v = -v;
    }
    string s;
    do {
      s += char('0' + (int)(v % 10));
      v /= 10;
    } while (v > 0);
    reverse(s.begin(), s.end());
    cerr << s;
  } else if constexpr (is_array_v<T> || requires { x.begin(), x.end(); }) {
    cerr << '[';
    bool head = true;
    for (const auto &e : x) {
      cerr << (head ? "" : ", ");
      debug_out(e);
      head = false;
    }
    cerr << ']';
  } else if constexpr (requires { cerr << x; }) {
    cerr << x;
  } else if constexpr (requires { tuple_size<T>::value; }) {
    cerr << '(';
    bool head = true;
    apply([&](const auto &...e) { ((cerr << (head ? "" : ", "), debug_out(e), head = false), ...); }, x);
    cerr << ')';
  } else {
    cerr << '?';
  }
}

// "a, f(b, c), v[i]" を引数ごとの名前に分ける（括弧と引用符の中のカンマでは区切らない）
inline vector<string> debug_split_names(const char *names) {
  vector<string> res(1);
  int depth = 0;
  char quote = 0;
  for (const char *p = names; *p; ++p) {
    char c = *p;
    if (quote) {
      if (c == '\\' && p[1]) {
        res.back() += c;
        c = *++p;
      } else if (c == quote) {
        quote = 0;
      }
    } else if (c == '"' || c == '\'') {
      quote = c;
    } else if (c == '(' || c == '[' || c == '{') {
      ++depth;
    } else if (c == ')' || c == ']' || c == '}') {
      --depth;
    } else if (c == ',' && depth == 0) {
      res.emplace_back();
      continue;
    }
    res.back() += c;
  }
  for (auto &s : res) {
    size_t b = s.find_first_not_of(' '), e = s.find_last_not_of(' ');
    s = b == string::npos ? "" : s.substr(b, e - b + 1);
  }
  return res;
}

template <class... Args>
void debug_print(int line, const char *names, const Args &...args) {
  vector<string> name = debug_split_names(names);
  size_t i = 0;
  cerr << "[L" << line << "]";
  ((cerr << (i ? ", " : " ") << (i < name.size() ? name[i] : "?") << " = ", debug_out(args), ++i), ...);
  cerr << '\n';
}
#define dump(...) debug_print(__LINE__, #__VA_ARGS__, __VA_ARGS__)
#else
#define dump(...) ((void)0)
#endif

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

// 調整したいパラメータ。環境変数 AHC_<name> があればその値、なければ既定値を返す
// scripts/tune.py（Optuna）が環境変数で値を渡す。提出時は環境変数がないので既定値になる
// 既定値の型で読み方が決まるので、小数のパラメータは 1000.0 のように書く
//
// 使い方:
//   double T0 = param("T0", 1000.0);
//   int BEAM_WIDTH = param("BEAM_WIDTH", 300);
template <class T>
T param(const char *name, T default_value) {
  const char *s = getenv((string("AHC_") + name).c_str());
  if (s == nullptr) {
    return default_value;
  }
  if constexpr (is_integral_v<T>) {
    return (T)atoll(s);
  } else {
    return (T)atof(s);
  }
}

int main() {
  ios::sync_with_stdio(false);
  cin.tie(0);
  cout.tie(0);
  return 0;
}