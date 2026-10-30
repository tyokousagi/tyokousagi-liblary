// I love hina-no.
// tyokousagi-グリッドライブラリ
//
// - Visited : 世代番号で管理する訪問済み配列。reset() が O(1)
// - Grid    : (行, 列) を1次元の番号 id = r * W + c で扱うための部品。隣のマスを事前計算する
//             4近傍 / 8近傍、端がつながったグリッド（トーラス）に対応
// - GridBFS : グリッド上の BFS。配列を使い回すので何度呼んでも初期化コストがかからない
//             複数始点、壁（マス単位 / マスの間）、距離の上限、ゴールでの打ち切り、経路復元に対応
// - GridDijkstra : 移動にコストがかかるグリッドの最短路（Dijkstra、コストが 0 と 1 だけなら 0-1 BFS）
//                  使い方は GridBFS と同じで、通れるかの判定の代わりにコストを返す関数を渡す
//
// 方向 d の順番は template.cpp の dx, dy, dir と同じ
//   0:U 1:D 2:L 3:R（8近傍のときはさらに 4:右下 5:左下 6:右上 7:左上）
// 逆方向は Grid::reverse(d)。
//
// 使い方:
//   Grid grid(N, N);            // 4近傍
//   Grid grid8(N, N, 8);        // 8近傍
//   Grid torus(N, N, 4, true);  // 端がつながったグリッド
//   GridBFS bfs(grid);
//
//   // 通れるかどうかは「入る先のマス」か「移動」のどちらかで書く
//   bfs.run(s, [&](int v) { return !wall[v]; });                              // マスごとの壁
//   bfs.run(s, [&](int from, int to, int d) { return !wall_between(from, d); });  // マスの間の壁
//
//   bfs.run(s, can, t);                     // t に着いた時点で打ち切る
//   bfs.run(s, can, -1, 5);                 // 距離 5 までだけ調べる
//   bfs.run(std::vector<int>{a, b}, can);   // 複数始点（一番近い始点からの距離）
//
//   bfs.dist(v);                            // 距離。届かなければ -1
//   bfs.source(v);                          // v に一番近い始点。届かなければ -1
//   for (int d : bfs.path_dirs(t)) cout << dir[d];  // 始点 → t の方向列
//   bfs.path(t);                            // 始点 → t のマス列
//   for (int v : bfs.order()) { ... }       // 届いたマスを近い順に
//
//   // コストは「入る先のマス」か「移動」で返す。負の値を返した移動は通れない
//   GridDijkstra dij(grid);
//   dij.run(s, [&](int v) { return wall[v] ? -1 : cost[v]; });
//   dij.run01(s, [&](int from, int to, int d) { return road[to] ? 0 : 1; });  // コストが 0 か 1 だけのとき
//   dij.dist(t);  // 以下 GridBFS と同じ（dist は long long）

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <functional>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

// 訪問済みフラグを世代番号で管理する配列。
// reset() で全体を未訪問に戻すのが O(1) なので、BFS / DFS を何度も回すときに fill が要らない。
struct Visited {
  std::vector<std::uint32_t> stamp;
  std::uint32_t epoch = 1;

  explicit Visited(int n = 0) : stamp(n, 0) {
  }

  // 全体を未訪問に戻す
  void reset() {
    if (++epoch == 0) {
      std::fill(stamp.begin(), stamp.end(), 0);
      epoch = 1;
    }
  }

  bool visited(int v) const {
    return stamp[v] == epoch;
  }

  void visit(int v) {
    stamp[v] = epoch;
  }

  // 未訪問なら訪問済みにして true、訪問済みなら false
  bool try_visit(int v) {
    if (stamp[v] == epoch) {
      return false;
    }
    stamp[v] = epoch;
    return true;
  }
};

struct Grid {
  static constexpr int DR[8] = {-1, 1, 0, 0, 1, 1, -1, -1};
  static constexpr int DC[8] = {0, 0, -1, 1, 1, -1, 1, -1};

  int H, W;
  int D;                                 // 方向の数（4 または 8）
  bool torus;                            // 端がつながっているか
  std::vector<std::array<int, 8>> next;  // next[id][d] = 方向 d に1マス進んだ先の id。範囲外なら -1

  Grid(int H, int W, int D = 4, bool torus = false) : H(H), W(W), D(D), torus(torus), next(H * W) {
    assert(D == 4 || D == 8);
    for (int r = 0; r < H; ++r) {
      for (int c = 0; c < W; ++c) {
        for (int d = 0; d < 8; ++d) {
          int nr = r + DR[d], nc = c + DC[d];
          if (torus) {
            nr = (nr + H) % H;
            nc = (nc + W) % W;
          }
          next[id(r, c)][d] = d < D && in(nr, nc) ? id(nr, nc) : -1;
        }
      }
    }
  }

  // 逆方向
  static int reverse(int d) {
    return d < 4 ? d ^ 1 : 11 - d;
  }

  int size() const {
    return H * W;
  }
  int id(int r, int c) const {
    return r * W + c;
  }
  int row(int v) const {
    return v / W;
  }
  int col(int v) const {
    return v % W;
  }
  bool in(int r, int c) const {
    return 0 <= r && r < H && 0 <= c && c < W;
  }

  // マンハッタン距離（トーラスなら回り込みも考える）
  int manhattan(int a, int b) const {
    int dr = std::abs(row(a) - row(b)), dc = std::abs(col(a) - col(b));
    if (torus) {
      dr = std::min(dr, H - dr);
      dc = std::min(dc, W - dc);
    }
    return dr + dc;
  }

  // チェビシェフ距離（8近傍での最短の手数。トーラスなら回り込みも考える）
  int chebyshev(int a, int b) const {
    int dr = std::abs(row(a) - row(b)), dc = std::abs(col(a) - col(b));
    if (torus) {
      dr = std::min(dr, H - dr);
      dc = std::min(dc, W - dc);
    }
    return std::max(dr, dc);
  }

  // 隣り合うマス from → to の方向。隣でなければ -1
  int direction(int from, int to) const {
    for (int d = 0; d < D; ++d) {
      if (next[from][d] == to) {
        return d;
      }
    }
    return -1;
  }
};

struct GridBFS {
  const Grid &grid;
  Visited vis;
  std::vector<int> dist_;
  std::vector<int> from_dir_;  // そのマスに入ってきたときの方向。始点は -1
  std::vector<int> source_;    // そのマスに一番近い始点
  std::vector<int> que_;       // 届いたマス（近い順）

  explicit GridBFS(const Grid &grid)
      : grid(grid), vis(grid.size()), dist_(grid.size()), from_dir_(grid.size()), source_(grid.size()) {
    que_.reserve(grid.size());
  }

  // s から BFS する。
  // can は「入る先のマス v に入れるか」bool(int v) か、「from から方向 d で to に動けるか」bool(int from, int to, int
  // d)。 始点自身は判定しない。goal を指定すると goal に着いた時点で打ち切る。max_dist より遠くは調べない。
  template <class F>
  void run(int s, F can, int goal = -1, int max_dist = std::numeric_limits<int>::max()) {
    run_from(&s, &s + 1, can, goal, max_dist);
  }

  // 複数始点の BFS。各マスには一番近い始点からの距離が入る
  template <class F>
  void run(const std::vector<int> &sources, F can, int goal = -1, int max_dist = std::numeric_limits<int>::max()) {
    run_from(sources.data(), sources.data() + sources.size(), can, goal, max_dist);
  }

  // 直前の run() での距離。届かなければ -1
  int dist(int v) const {
    return vis.visited(v) ? dist_[v] : -1;
  }

  bool reachable(int v) const {
    return vis.visited(v);
  }

  // v に一番近い始点。届かなければ -1
  int source(int v) const {
    return vis.visited(v) ? source_[v] : -1;
  }

  // 届いたマス（近い順）。goal で打ち切った場合はそこまでに見つけたマス
  const std::vector<int> &order() const {
    return que_;
  }

  // 始点 → t の方向列。t に届いていること
  std::vector<int> path_dirs(int t) const {
    assert(reachable(t));
    std::vector<int> dirs(dist_[t]);
    int v = t;
    for (int i = dist_[t] - 1; i >= 0; --i) {
      dirs[i] = from_dir_[v];
      v = grid.next[v][Grid::reverse(from_dir_[v])];
    }
    return dirs;
  }

  // 始点 → t のマス列（始点と t を含む）。t に届いていること
  std::vector<int> path(int t) const {
    assert(reachable(t));
    std::vector<int> cells(dist_[t] + 1);
    int v = t;
    for (int i = dist_[t]; i >= 0; --i) {
      cells[i] = v;
      if (i > 0) {
        v = grid.next[v][Grid::reverse(from_dir_[v])];
      }
    }
    return cells;
  }

 private:
  template <class F>
  static bool allowed(F &can, int from, int to, int d) {
    if constexpr (std::is_invocable_v<F &, int, int, int>) {
      return can(from, to, d);
    } else {
      return can(to);
    }
  }

  template <class F>
  void run_from(const int *first, const int *last, F &can, int goal, int max_dist) {
    vis.reset();
    que_.clear();
    for (const int *it = first; it != last; ++it) {
      int s = *it;
      if (!vis.try_visit(s)) {
        continue;
      }
      dist_[s] = 0;
      from_dir_[s] = -1;
      source_[s] = s;
      que_.push_back(s);
      if (s == goal) {
        return;
      }
    }
    for (int head = 0; head < (int)que_.size(); ++head) {
      int v = que_[head];
      if (dist_[v] >= max_dist) {
        break;  // que_ は距離の小さい順なので、ここから先も全部 max_dist 以上
      }
      for (int d = 0; d < grid.D; ++d) {
        int u = grid.next[v][d];
        if (u < 0 || vis.visited(u) || !allowed(can, v, u, d)) {
          continue;
        }
        vis.visit(u);
        dist_[u] = dist_[v] + 1;
        from_dir_[u] = d;
        source_[u] = source_[v];
        que_.push_back(u);
        if (u == goal) {
          return;
        }
      }
    }
  }
};

// 移動にコストがかかるグリッドの最短路。
// cost は「入る先のマス v に入るコスト」long long(int v) か、「from から方向 d で to に動くコスト」
// long long(int from, int to, int d)。負の値を返した移動は通れない。始点自身のコストは数えない。
// run() は Dijkstra（コストは 0 以上）、run01() はコストが 0 か 1 だけのときの 0-1 BFS（Dijkstra より速い）。
// 距離が確定したマスだけを「届いた」として扱う。goal や max_dist
// で打ち切ったときは、そこまでに確定したマスだけが届いたことになる。
struct GridDijkstra {
  using Dist = long long;
  static constexpr Dist NO_LIMIT = std::numeric_limits<Dist>::max();

  const Grid &grid;
  Visited seen_;  // 仮の距離が入ったマス
  Visited done_;  // 距離が確定したマス
  std::vector<Dist> dist_;
  std::vector<int> from_dir_;  // 最短路でそのマスに入ってきたときの方向。始点は -1
  std::vector<int> source_;    // そのマスに一番近い始点
  std::vector<int> order_;     // 距離が確定した順
  std::vector<std::pair<Dist, int>> heap_;
  std::deque<int> deque_;

  explicit GridDijkstra(const Grid &grid)
      : grid(grid),
        seen_(grid.size()),
        done_(grid.size()),
        dist_(grid.size()),
        from_dir_(grid.size()),
        source_(grid.size()) {
    order_.reserve(grid.size());
  }

  template <class F>
  void run(int s, F cost, int goal = -1, Dist max_dist = NO_LIMIT) {
    run_dijkstra(&s, &s + 1, cost, goal, max_dist);
  }
  template <class F>
  void run(const std::vector<int> &sources, F cost, int goal = -1, Dist max_dist = NO_LIMIT) {
    run_dijkstra(sources.data(), sources.data() + sources.size(), cost, goal, max_dist);
  }

  // コストが 0 か 1（と、通れない負の値）だけのとき
  template <class F>
  void run01(int s, F cost, int goal = -1, Dist max_dist = NO_LIMIT) {
    run_01(&s, &s + 1, cost, goal, max_dist);
  }
  template <class F>
  void run01(const std::vector<int> &sources, F cost, int goal = -1, Dist max_dist = NO_LIMIT) {
    run_01(sources.data(), sources.data() + sources.size(), cost, goal, max_dist);
  }

  // 直前の run() / run01() での距離。届かなければ -1
  Dist dist(int v) const {
    return done_.visited(v) ? dist_[v] : -1;
  }

  bool reachable(int v) const {
    return done_.visited(v);
  }

  // v に一番近い始点。届かなければ -1
  int source(int v) const {
    return done_.visited(v) ? source_[v] : -1;
  }

  // 距離が確定したマス（近い順）
  const std::vector<int> &order() const {
    return order_;
  }

  // 始点 → t の方向列。t に届いていること
  std::vector<int> path_dirs(int t) const {
    assert(reachable(t));
    std::vector<int> dirs;
    for (int v = t; from_dir_[v] != -1; v = grid.next[v][Grid::reverse(from_dir_[v])]) {
      dirs.push_back(from_dir_[v]);
    }
    std::reverse(dirs.begin(), dirs.end());
    return dirs;
  }

  // 始点 → t のマス列（始点と t を含む）。t に届いていること
  std::vector<int> path(int t) const {
    assert(reachable(t));
    std::vector<int> cells = {t};
    for (int v = t; from_dir_[v] != -1;) {
      v = grid.next[v][Grid::reverse(from_dir_[v])];
      cells.push_back(v);
    }
    std::reverse(cells.begin(), cells.end());
    return cells;
  }

 private:
  template <class F>
  static Dist move_cost(F &cost, int from, int to, int d) {
    if constexpr (std::is_invocable_v<F &, int, int, int>) {
      return cost(from, to, d);
    } else {
      return cost(to);
    }
  }

  // 始点を入れる。push(s) で始点をキューに積む
  template <class Push>
  void start(const int *first, const int *last, Push push) {
    seen_.reset();
    done_.reset();
    order_.clear();
    for (const int *it = first; it != last; ++it) {
      int s = *it;
      if (!seen_.try_visit(s)) {
        continue;
      }
      dist_[s] = 0;
      from_dir_[s] = -1;
      source_[s] = s;
      push(s);
    }
  }

  template <class F>
  void run_dijkstra(const int *first, const int *last, F &cost, int goal, Dist max_dist) {
    heap_.clear();
    start(first, last, [&](int s) { heap_.emplace_back(0, s); });
    std::make_heap(heap_.begin(), heap_.end(), std::greater<>());
    while (!heap_.empty()) {
      std::pop_heap(heap_.begin(), heap_.end(), std::greater<>());
      auto [d, v] = heap_.back();
      heap_.pop_back();
      if (done_.visited(v) || d > dist_[v]) {
        continue;  // 古い候補
      }
      if (d > max_dist) {
        break;  // ここから先は全部 max_dist より遠い
      }
      done_.visit(v);
      order_.push_back(v);
      if (v == goal) {
        return;
      }
      for (int dir = 0; dir < grid.D; ++dir) {
        int u = grid.next[v][dir];
        if (u < 0 || done_.visited(u)) {
          continue;
        }
        Dist c = move_cost(cost, v, u, dir);
        if (c < 0) {
          continue;
        }
        Dist nd = d + c;
        if (seen_.visited(u) && nd >= dist_[u]) {
          continue;
        }
        seen_.visit(u);
        dist_[u] = nd;
        from_dir_[u] = dir;
        source_[u] = source_[v];
        heap_.emplace_back(nd, u);
        std::push_heap(heap_.begin(), heap_.end(), std::greater<>());
      }
    }
  }

  template <class F>
  void run_01(const int *first, const int *last, F &cost, int goal, Dist max_dist) {
    deque_.clear();
    start(first, last, [&](int s) { deque_.push_back(s); });
    while (!deque_.empty()) {
      int v = deque_.front();
      deque_.pop_front();
      if (done_.visited(v)) {
        continue;  // 古い候補
      }
      if (dist_[v] > max_dist) {
        break;  // ここから先は全部 max_dist より遠い
      }
      done_.visit(v);
      order_.push_back(v);
      if (v == goal) {
        return;
      }
      for (int dir = 0; dir < grid.D; ++dir) {
        int u = grid.next[v][dir];
        if (u < 0 || done_.visited(u)) {
          continue;
        }
        Dist c = move_cost(cost, v, u, dir);
        if (c < 0) {
          continue;
        }
        assert(c <= 1);
        Dist nd = dist_[v] + c;
        if (seen_.visited(u) && nd >= dist_[u]) {
          continue;
        }
        seen_.visit(u);
        dist_[u] = nd;
        from_dir_[u] = dir;
        source_[u] = source_[v];
        if (c == 0) {
          deque_.push_front(u);
        } else {
          deque_.push_back(u);
        }
      }
    }
  }
};
