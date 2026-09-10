/*
  I love hina-no
  tyokousagi-ビームサーチライブラリ
  ============================================================
  参考資料
  ============================================================

  このライブラリの設計・実装・高速化にあたり、以下の資料を参考にしました。

  - thunder_library / edge_beam.cpp
  Euler Tour の辺を保持する差分更新ビームサーチ実装。
  https://github.com/thun-c/thunder_library
  https://github.com/thun-c/thunder_library/blob/main/thunder/lib/edge_beam.cpp

  - eijirou さん「差分更新ビームサーチライブラリ」
  thunder_library の README / edge_beam.cpp
  からも参考元として挙げられている資料。
  https://eijirou-kyopro.hatenablog.com/entry/2024/02/01/115639

  - Rafael Bocquet (Rafbill) / Euler Tour Beam Search
  Euler Tour を用いた差分更新ビームサーチの実装・考察。
  https://gitlab.com/rafaelbocquet-cpcontests/euler-tour-beam-search

  - AtCoder Heuristic Contest 021 Editorial
  Euler Tour を用いる差分更新ビームサーチの解説。
  https://atcoder.jp/contests/ahc021/editorial/6681

  - AtCoder Heuristic Contest 070 Editorial
  1個の状態を undo / apply で使い回し、LCA を経由して状態間を移動する方法や、
  レベルごとに走査方向を反転する方法を参考にしました。
  https://atcoder.jp/contests/ahc070/editorial/25369

  - miniselect / Floyd-Rivest selection
  Top-K 選択の高速化で Floyd-Rivest selection の実装を参考にしました。
  https://github.com/danlark1/miniselect
  https://github.com/danlark1/miniselect/blob/master/include/miniselect/floyd_rivest_select.h
  miniselect は Boost Software License 1.0 で公開されています。

  ============================================================
  まず最初に：利用者が実装するもの
  ============================================================

  このライブラリを使うために、利用者が実装する必要があるのは次の3つです。

  1. Action 型
  2. Score 型
  3. template<class Selector> struct StateBase

  ライブラリ内部の Tree / Candidate / Selector / Euler Tour / LCA は
  利用者が実装する必要はありません。

  ------------------------------------------------------------
  1. Action 型
  ------------------------------------------------------------

  1手分の操作を表す型です。
  std::copyable を満たす必要があります。

  例:

  struct Action {
    int x;
    int y;
  };

  operator== などは不要です。
  小さい型にすると Candidate / TourEntry のコピーが軽くなるので高速です。

  ------------------------------------------------------------
  2. Score 型
  ------------------------------------------------------------

  ビームサーチで候補を比較する評価値です。

  必須条件:
  - std::copyable
  - operator< で比較できる

  【重要】このライブラリは「Score が小さいほど良い候補」として扱います。
  最大化したい場合は -score などに変換してください。

  例:

  using Score = long long;

  数値型である必要はありません。例えば以下も使用できます。

  struct Score {
    long long main_score;
    int sub_score;

    auto operator<=>(const Score&) const = default;
  };

  ------------------------------------------------------------
  3. StateBase<Selector>
  ------------------------------------------------------------

  問題固有の状態を保持するクラスです。
  次の4メソッドを必ず実装してください。

  Score make_initial_node();
  void expand(int parent, Selector& selector);
  void apply(const Action& action);
  void undo (const Action& action);

  apply / undo に Score は渡しません。
  Score は候補の比較・選択にだけ使い、状態遷移は Action だけで表します。

  StateBase<Selector> は copy constructible である必要があります。
  ただし State のコピーは探索開始時に1回だけ行い、
  各候補の展開では apply / undo により1個のStateを使い回します。

  ============================================================
  各メソッドの意味
  ============================================================

  ------------------------------------------------------------
  make_initial_node()
  ------------------------------------------------------------

  探索開始時に1回だけ呼ばれます。
  初期状態に必要な初期化処理があればここで行ってください。
  初期状態の Score を返してください。

  現在の実装では返り値そのものをbeam選択には使用しませんが、
  APIの一貫性のため Score を返す仕様にしています。

  例:

  Score make_initial_node() {
    current_score = 0;
    return current_score;
  }

  ------------------------------------------------------------
  expand(parent, selector)
  ------------------------------------------------------------

  「現在の State から次に行ける Action」をすべて列挙します。

  Tree側が、expand()を呼ぶ前に State を対象ノードの状態まで
  apply済みにしてあります。

  parent は現在展開しているbeamノードの番号です。

  最も分かりやすい書き方:

  void expand(int parent, Selector& selector) {
    for (...) {
      Action action = ...;
      Score next_score = ...;  // action適用後の子ノードの評価値

      selector.push(
        action,
        next_score,
        parent,
        false
      );
    }
  }

  高速API:
  Tree側で selector.begin_parent(parent) を呼んでから expand() に入るため、
  parent を毎回渡さず次のようにも書けます。

  void expand(int parent, Selector& selector) {
    for (...) {
      selector.push(action, next_score);
    }
  }

  固定ターン型AHCでは通常 finished=false の候補だけ追加すれば十分です。

  可変長問題などで、そのActionを選んだ時点で完成解になる場合は、

  selector.push(action, score, parent, true);

  または高速APIで

  selector.push_finished(action, score);

  としてください。

  【重要な契約】
  expand() の呼び出し前後で State の内容を変えないでください。
  score計算のため一時的に apply/undo するのは構いませんが、
  expand() を抜けるまでに必ず元の状態へ戻してください。

  ------------------------------------------------------------
  apply(action)
  ------------------------------------------------------------

  現在の State を、その Action を選んだ「子ノードの状態」へ進めます。

  例:

  void apply(const Action& action) {
    board[action.x] = ...;
  }

  ------------------------------------------------------------
  undo(action)
  ------------------------------------------------------------

  apply(action) を完全に取り消し、親ノードの State に戻します。

  必ず次の性質を満たしてください。

  State before = current state;
  apply(action);
  undo(action);
  // current state == before

  この可逆性がEuler Tour/LCA探索の正しさの前提です。

  ============================================================
  実装の雛形（コピペ用）
  ============================================================

  以下を問題コード側へコピーし、TODO部分を実装すれば使えます。
  固定ターン型AHCを想定した最も基本的な形です。

  ------------------------------------------------------------
  1. 1手を表す Action を定義
  ------------------------------------------------------------

  struct Action {
    int move;  // TODO: 1手を復元するために必要な情報を入れる
  };

  ------------------------------------------------------------
  2. 評価値 Score を定義
  ------------------------------------------------------------

  using Score = long long;

  Score は「小さいほど良い」です。
  最大化問題では負号を付けるなどして小さいほど良い値へ変換してください。

  ------------------------------------------------------------
  3. 問題状態 StateBase<Selector> を実装
  ------------------------------------------------------------

  template<class Selector>
  struct StateBase {
    // TODO: 現在の状態を表す変数を書く
    // 例:
    // int turn = 0;
    // long long current_cost = 0;
    // std::array<int, N> board{};
    // Score value{};  // 現在Stateの評価値を持つならこのように用意する

    // 必要なら undo 用の差分ログも State に持たせる。
    // apply() で変更した内容を記録し、undo() で完全に戻す。

    Score make_initial_node() {
      // TODO: 初期状態を作る。
      // State のメンバ初期化がコンストラクタ等で済んでいるなら、
      // 初期 Score を返すだけでもよい。

      return Score{};
    }

    void expand(int parent, Selector& selector) {
      // この時点の State は、展開対象ノードの状態になっている。
      // TODO: 現在状態から可能な全 Action を列挙する。

      for (int move = 0; move < MAX_BRANCH; ++move) {
        Action action{move};

        // action を一時的に適用して子状態を作る。
        apply(action);

        // TODO: action 適用後の現在Stateから子ノードのScoreを取得する。
        // 例: Stateに Score value; を持たせ、apply() 内で更新しているなら
        //     Score next_score = value;
        Score next_score = value;

        // expand() を抜ける時点では親Stateへ戻す。
        undo(action);

        // 通常API。固定ターンAHCなら finished=false でよい。
        selector.push(action, next_score, parent, false);

        // Tree側が begin_parent(parent) 済みなので、
        // 少しでも定数倍を削りたい場合は次の高速APIでもよい。
        // selector.push(action, next_score);
      }
    }

    void apply(const Action& action) {
      // TODO: 現在の State を action 適用後の子状態へ進める。
      //
      // 重要:
      // undo() で完全に元へ戻せるようにすること。
      // 大きな配列を毎回コピーするのではなく、
      // 書き換えた場所と旧値だけを差分ログへ保存すると高速。

      (void)action;
    }

    void undo(const Action& action) {
      // TODO: 直前の apply(action) を完全に取り消す。
      //
      // 必ず
      //   apply(action);
      //   undo(action);
      // の後に State が1bitの意味でも元の論理状態へ戻るようにする。

      (void)action;
    }
  };

  ------------------------------------------------------------
  4. Beam 型を作る
  ------------------------------------------------------------

  using Beam = etbs::EulerTourBeamSearch<Action, Score, StateBase>;

  ------------------------------------------------------------
  5. Config を設定して実行
  ------------------------------------------------------------

  Beam::Config config;
  config.max_turn = T;                 // TODO: 探索ターン数
  config.beam_width = BEAM_WIDTH;      // TODO: ビーム幅
  config.candidate_capacity =          // reserve目安
    static_cast<std::size_t>(BEAM_WIDTH) * MAX_BRANCH;

  // 通常はデフォルトの FloydRivest でよい。
  // 小さい beam / 分岐数では他方式が速い場合もあるので、
  // 必要なら実測して変更する。
  // config.selection_policy = Beam::SelectionPolicy::FloydRivest;

  // 0ならpure Euler Tour。
  // デフォルトでは深く肥大化した場合のみLCA backendへ切り替える。
  // config.lca_switch_ratio = 32;

  StateBase<Beam::Selector> state;
  StateBase<Beam::Selector> answer = Beam{}.beam_search(config, state);

  // answer は探索で選ばれた最良経路を適用済みの最終 State。

  ============================================================
  apply / undo の典型例
  ============================================================

  大きな配列を持つ場合は、State全体をコピーするのではなく、
  「変更した要素の index と旧値」だけを保存するのが基本です。

  例:

  struct Change {
    int index;
    int old_value;
  };

  std::vector<Change> undo_log;
  std::vector<std::size_t> undo_mark;

  void apply(const Action& action) {
    undo_mark.push_back(undo_log.size());

    // 例: data[i]を書き換える直前に旧値を保存
    // undo_log.push_back({i, data[i]});
    // data[i] = new_value;
  }

  void undo(const Action& action) {
    std::size_t mark = undo_mark.back();
    undo_mark.pop_back();

    while (undo_log.size() > mark) {
      auto change = undo_log.back();
      undo_log.pop_back();
      // data[change.index] = change.old_value;
    }
  }

  apply/undoが軽いほど、このライブラリの差分更新方式の効果が大きくなります。

  ============================================================
  Config の意味
  ============================================================

  max_turn
  探索ターン数。固定ターンAHCでは問題のターン数を指定してください。

  beam_width
  ビーム幅。各ターンで残す候補数です。

  candidate_capacity
  1ターンで生成するCandidate数の予約容量です。
  通常モードではreserveのヒントなので、超えても正しく動作します。
  例えば「beam幅 K、最大分岐数 B」なら K * B が目安です。

  tour_capacity
  Euler Tour配列のreserveヒントです。0でも正しく動作します。

  selection_policy
  Top-K選択方式です。
  デフォルトは FloydRivest です。

  lca_switch_ratio
  Euler Tourが深く肥大化したときにLCA backendへ一度だけ切り替える閾値です。
  デフォルト32。0にするとpure Euler Tourになります。

  return_finished_immediately
  trueならfinished候補が見つかったターンで最良finished候補を返します。
  固定ターンAHCでは通常falseのままで構いません。

  ============================================================
  高速固定容量Candidate Buffer（任意）
  ============================================================

  通常は以下で十分です。

  using Beam = etbs::EulerTourBeamSearch<Action, Score, StateBase>;

  candidate_capacityを「絶対に超えない上限」として指定できる場合は、
  固定容量buffer版も利用できます。

  using Beam = etbs::EulerTourBeamSearch<Action, Score, StateBase, true>;

  この場合 candidate_capacity は必須であり、上限を超えると abort() します。
  AHCで最大候補数を厳密に見積もれる場合のみ使用してください。

  ============================================================
  利用者が実装しなくてよいもの
  ============================================================

  以下はすべてライブラリ内部で処理します。

  - Candidate管理
  - beamのTop-K選択
  - Euler Tourの構築・更新
  - apply/undoしながらのDFS
  - 共通prefix圧縮
  - 深いEuler TourからLCA backendへの切替
  - 最良経路の復元と最終Stateへの反映
  - 同点候補の決定的なtie-break

*/

#include <algorithm>
#include <cassert>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace etbs {

template <class T>
concept ActionConcept = std::copyable<T>;

template <class T>
concept ScoreConcept = std::copyable<T> && requires(const T &a, const T &b) {
  { a < b } -> std::convertible_to<bool>;
};

template <class T>
class DynamicBuffer {
 public:
  explicit DynamicBuffer(std::size_t capacity) {
    data_.reserve(capacity);
  }
  template <class... Args>
  T &emplace_back(Args &&...args) {
    return data_.emplace_back(std::forward<Args>(args)...);
  }
  void clear() noexcept {
    data_.clear();
  }
  void resize(std::size_t n) {
    assert(n <= data_.size());
    data_.erase(data_.begin() + static_cast<std::ptrdiff_t>(n), data_.end());
  }
  [[nodiscard]] std::size_t size() const noexcept {
    return data_.size();
  }
  [[nodiscard]] std::size_t capacity() const noexcept {
    return data_.capacity();
  }
  [[nodiscard]] bool empty() const noexcept {
    return data_.empty();
  }
  [[nodiscard]] bool full() const noexcept {
    return false;
  }
  T *begin() noexcept {
    return data_.data();
  }
  const T *begin() const noexcept {
    return data_.data();
  }
  T *end() noexcept {
    return data_.data() + data_.size();
  }
  const T *end() const noexcept {
    return data_.data() + data_.size();
  }
  T &operator[](std::size_t i) noexcept {
    return data_[i];
  }
  const T &operator[](std::size_t i) const noexcept {
    return data_[i];
  }
  T &front() noexcept {
    return data_.front();
  }
  const T &front() const noexcept {
    return data_.front();
  }
  std::span<T> span() noexcept {
    return {data_.data(), data_.size()};
  }
  std::span<const T> span() const noexcept {
    return {data_.data(), data_.size()};
  }

 private:
  std::vector<T> data_;
};

template <class T>
class FixedBuffer {
 public:
  explicit FixedBuffer(std::size_t capacity)
      : capacity_(capacity), data_(capacity_ ? allocator_.allocate(capacity_) : nullptr) {
  }
  FixedBuffer(const FixedBuffer &) = delete;
  FixedBuffer &operator=(const FixedBuffer &) = delete;
  ~FixedBuffer() {
    clear();
    if (data_)
      allocator_.deallocate(data_, capacity_);
  }
  template <class... Args>
  T &emplace_back(Args &&...args) {
    if (size_ == capacity_) [[unlikely]]
      std::abort();
    T *p = data_ + size_++;
    return *std::construct_at(p, std::forward<Args>(args)...);
  }
  void clear() noexcept {
    std::destroy_n(data_, size_);
    size_ = 0;
  }
  void resize(std::size_t n) noexcept {
    assert(n <= size_);
    std::destroy(data_ + n, data_ + size_);
    size_ = n;
  }
  [[nodiscard]] std::size_t size() const noexcept {
    return size_;
  }
  [[nodiscard]] std::size_t capacity() const noexcept {
    return capacity_;
  }
  [[nodiscard]] bool empty() const noexcept {
    return size_ == 0;
  }
  [[nodiscard]] bool full() const noexcept {
    return size_ == capacity_;
  }
  T *begin() noexcept {
    return data_;
  }
  const T *begin() const noexcept {
    return data_;
  }
  T *end() noexcept {
    return data_ + size_;
  }
  const T *end() const noexcept {
    return data_ + size_;
  }
  T &operator[](std::size_t i) noexcept {
    assert(i < size_);
    return data_[i];
  }
  const T &operator[](std::size_t i) const noexcept {
    assert(i < size_);
    return data_[i];
  }
  T &front() noexcept {
    assert(size_);
    return data_[0];
  }
  const T &front() const noexcept {
    assert(size_);
    return data_[0];
  }
  std::span<T> span() noexcept {
    return {data_, size_};
  }
  std::span<const T> span() const noexcept {
    return {data_, size_};
  }

 private:
  std::allocator<T> allocator_;
  std::size_t capacity_ = 0;
  std::size_t size_ = 0;
  T *data_ = nullptr;
};

template <ActionConcept Action, ScoreConcept Score, template <class> class State, bool FixedCandidateBuffer = false>
class EulerTourBeamSearch {
 public:
  enum class SelectionPolicy {
    // std::partial_sort: 選ばれたTop-K全体を順位順に並べる。
    PartialSort,
    // std::nth_element:
    // Top-K集合を選び、Tree::update()で親ごとにだけ順序付けする。
    NthElement,
    // 固定サイズheapによるexact Top-K。選択後は親ごとにだけ順序付けする。
    HeapSelect,
    // Floyd-Rivestによるexact Top-K。通常はこれを推奨。
    FloydRivest,
    // 巨大な候補列を途中でFloyd-RivestによりK件へ圧縮する方式。
    BatchedFloyd,
  };

  // ============================================================
  // Config: 利用者が設定する探索パラメータ
  // ============================================================
  // 必須なのは基本的に max_turn と beam_width の2つです。
  // candidate_capacity は「1ターンに生成する候補数」の目安を入れると
  // 再確保を避けやすくなります。その他は必要になったときだけ変更してください。
  struct Config {
    // 探索するターン数。固定ターンAHCなら問題のターン数を指定する。
    int max_turn = 0;

    // 各ターンで残す候補数。
    std::size_t beam_width = 1;

    // Euler Tour配列のreserve容量。0でも動作する。
    std::size_t tour_capacity = 0;

    // 1ターンの候補数のreserve容量。目安は beam_width * 最大分岐数。
    // FixedCandidateBuffer=true の場合は「絶対に超えない上限」として扱う。
    std::size_t candidate_capacity = 0;

    // trueならfinished候補を発見したターンで返す。固定ターンAHCでは通常false。
    bool return_finished_immediately = false;

    // Top-K選択方法。通常はFloydRivest推奨。
    SelectionPolicy selection_policy = SelectionPolicy::FloydRivest;
    // BatchedFloyd専用。候補数がおよそ beam_width * batch_factor
    // に達するたびK件へ圧縮する。 8程度を初期値として推奨。
    std::size_t batch_factor = 8;
    // 0でHybrid無効。Euler Tourのactive entry数が beam_width
    // のこの倍数を超えると、 以後LCA backendへ一方向に切り替える。
    std::size_t lca_switch_ratio = 32;
    // LCA arenaのreserveヒント。0なら必要に応じてvectorを拡張する。
    std::size_t lca_capacity = 0;
  };

  struct Candidate {
    Action action;
    Score score;
    std::uint64_t tie_key;

    Candidate(const Action &action_, const Score &score_, int parent_, std::uint32_t order_)
        : action(action_), score(score_), tie_key((std::uint64_t(static_cast<std::uint32_t>(parent_)) << 32) | order_) {
    }

    [[nodiscard]] std::uint32_t parent() const noexcept {
      return static_cast<std::uint32_t>(tie_key >> 32);
    }
    [[nodiscard]] std::uint32_t order() const noexcept {
      return static_cast<std::uint32_t>(tie_key);
    }
  };

  struct FloydRivest {
    template <class Iter, class Compare>
    static void loop(Iter begin, std::ptrdiff_t left, std::ptrdiff_t right, std::ptrdiff_t k, Compare comp) {
      while (right > left) {
        const std::ptrdiff_t size = right - left;
        if (size > 600) {
          const std::ptrdiff_t n = right - left + 1;
          const std::ptrdiff_t i = k - left + 1;
          const double z = std::log(static_cast<double>(n));
          const double sample = 0.5 * std::exp(2.0 * z / 3.0);
          double sd = 0.5 * std::sqrt(z * sample * (n - sample) / n);
          if (i < n / 2)
            sd = -sd;
          const auto new_left = std::max(left, static_cast<std::ptrdiff_t>(k - i * sample / n + sd));
          const auto new_right = std::min(right, static_cast<std::ptrdiff_t>(k + (n - i) * sample / n + sd));
          loop(begin, new_left, new_right, k, comp);
        }
        std::ptrdiff_t i = left;
        std::ptrdiff_t j = right;
        std::swap(begin[left], begin[k]);
        const bool swap_ends = comp(begin[left], begin[right]);
        if (swap_ends)
          std::swap(begin[left], begin[right]);
        const auto &pivot = swap_ends ? begin[left] : begin[right];
        while (i < j) {
          std::swap(begin[i], begin[j]);
          ++i;
          --j;
          while (comp(begin[i], pivot))
            ++i;
          while (comp(pivot, begin[j]))
            --j;
        }
        if (swap_ends) {
          std::swap(begin[left], begin[j]);
        } else {
          ++j;
          std::swap(begin[right], begin[j]);
        }
        if (j <= k)
          left = j + 1;
        if (k <= j)
          right = j - 1;
      }
    }

    template <class Iter, class Compare>
    static void select(Iter begin, Iter middle, Iter end, Compare comp) {
      if (middle == end || begin == end)
        return;
      loop(begin, 0, static_cast<std::ptrdiff_t>(end - begin - 1), static_cast<std::ptrdiff_t>(middle - begin), comp);
    }
  };

  class Selector {
   public:
    using Buffer = std::conditional_t<FixedCandidateBuffer, FixedBuffer<Candidate>, DynamicBuffer<Candidate>>;

    explicit Selector(const Config &config)
        : beam_width_(config.beam_width),
          selection_policy_(config.selection_policy),
          batch_limit_(config.beam_width * std::max<std::size_t>(config.batch_factor, 2)),
          candidates_(candidate_capacity(config)) {
      if constexpr (FixedCandidateBuffer) {
        if (config.candidate_capacity == 0)
          std::abort();
        if (selection_policy_ == SelectionPolicy::BatchedFloyd && config.candidate_capacity <= beam_width_)
          std::abort();
        batch_limit_ = std::min(batch_limit_, config.candidate_capacity);
      }
      finished_candidates_.reserve(config.beam_width);
    }

    void begin_parent(int parent) noexcept {
      assert(parent >= 0);
      current_parent_ = static_cast<std::uint32_t>(parent);
      local_order_ = 0;
    }

    // 高速API: TreeがState::expand()の前にbegin_parent(parent)を呼んでいる。
    void push(const Action &action, const Score &score) {
      assert(current_parent_ != kNoParent);
      prepare_for_push();
      assert(local_order_ != std::numeric_limits<std::uint32_t>::max());
      candidates_.emplace_back(action, score, static_cast<int>(current_parent_), local_order_++);
      selected_ = false;
      maybe_reduce_batch();
    }

    void push_finished(const Action &action, const Score &score) {
      assert(current_parent_ != kNoParent);
      assert(local_order_ != std::numeric_limits<std::uint32_t>::max());
      finished_candidates_.emplace_back(action, score, static_cast<int>(current_parent_), local_order_++);
      finished_sorted_ = false;
    }

    // 互換API: 参照実装に近い書き方。parentとfinishedを明示する。
    void push(const Action &action, const Score &score, int parent, bool finished = false) {
      assert(parent >= 0);
      const auto p = static_cast<std::uint32_t>(parent);
      if (p != current_parent_)
        begin_parent(parent);
      if (finished)
        push_finished(action, score);
      else
        push(action, score);
    }

    std::span<const Candidate> select() {
      if (selected_)
        return candidates_.span();
      if (selection_policy_ == SelectionPolicy::BatchedFloyd)
        reduce_to_beam();

      const std::size_t k = std::min(beam_width_, candidates_.size());
      if (candidates_.size() > k) {
        auto middle = candidates_.begin() + static_cast<std::ptrdiff_t>(k);
        switch (selection_policy_) {
          case SelectionPolicy::PartialSort:
            std::partial_sort(candidates_.begin(), middle, candidates_.end(), Better{});
            break;
          case SelectionPolicy::NthElement:
            std::nth_element(candidates_.begin(), middle, candidates_.end(), Better{});
            break;
          case SelectionPolicy::HeapSelect:
            heap_select(k);
            break;
          case SelectionPolicy::FloydRivest:
          case SelectionPolicy::BatchedFloyd:
            FloydRivest::select(candidates_.begin(), middle, candidates_.end(), Better{});
            break;
        }
        candidates_.resize(k);
      } else if (!candidates_.empty() && selection_policy_ == SelectionPolicy::PartialSort) {
        std::sort(candidates_.begin(), candidates_.end(), Better{});
      }
      selected_ = true;
      return candidates_.span();
    }

    [[nodiscard]] bool have_finished() const noexcept {
      return !finished_candidates_.empty();
    }

    const std::vector<Candidate> &get_finished_candidates() {
      if (!finished_sorted_) {
        std::sort(finished_candidates_.begin(), finished_candidates_.end(), Better{});
        finished_sorted_ = true;
      }
      return finished_candidates_;
    }

    const Candidate &calculate_best_candidate() const {
      assert(selected_ && !candidates_.empty());
      if (selection_policy_ == SelectionPolicy::PartialSort)
        return candidates_.front();
      return *std::min_element(candidates_.begin(), candidates_.end(), Better{});
    }

    void clear() noexcept {
      candidates_.clear();
      current_parent_ = kNoParent;
      local_order_ = 0;
      selected_ = false;
    }

    void clear_finished_candidates() noexcept {
      finished_candidates_.clear();
      finished_sorted_ = true;
    }

    static bool better(const Candidate &a, const Candidate &b) {
      return Better{}(a, b);
    }

   private:
    static constexpr std::uint32_t kNoParent = std::numeric_limits<std::uint32_t>::max();

    static std::size_t candidate_capacity(const Config &config) {
      if constexpr (FixedCandidateBuffer)
        return config.candidate_capacity;
      return config.candidate_capacity == 0 ? config.beam_width : config.candidate_capacity;
    }

    struct Better {
      bool operator()(const Candidate &a, const Candidate &b) const {
        if (a.score < b.score)
          return true;
        if (b.score < a.score)
          return false;
        return a.tie_key < b.tie_key;
      }
    };

    void prepare_for_push() {
      if constexpr (FixedCandidateBuffer) {
        if (!candidates_.full())
          return;
        if (selection_policy_ == SelectionPolicy::BatchedFloyd && candidates_.size() > beam_width_) {
          reduce_to_beam();
          if (!candidates_.full())
            return;
        }
        std::abort();
      }
    }

    void maybe_reduce_batch() {
      if (selection_policy_ == SelectionPolicy::BatchedFloyd && candidates_.size() >= batch_limit_) {
        reduce_to_beam();
      }
    }

    void reduce_to_beam() {
      if (candidates_.size() <= beam_width_)
        return;
      auto middle = candidates_.begin() + static_cast<std::ptrdiff_t>(beam_width_);
      FloydRivest::select(candidates_.begin(), middle, candidates_.end(), Better{});
      candidates_.resize(beam_width_);
    }

    void heap_select(std::size_t k) {
      auto worse = [](const Candidate &a, const Candidate &b) { return Better{}(b, a); };
      auto sift_down = [&](std::size_t root) {
        while (true) {
          const std::size_t left = root * 2 + 1;
          if (left >= k)
            break;
          std::size_t child = left;
          const std::size_t right = left + 1;
          if (right < k && worse(candidates_[right], candidates_[left]))
            child = right;
          if (!worse(candidates_[child], candidates_[root]))
            break;
          std::swap(candidates_[root], candidates_[child]);
          root = child;
        }
      };
      if (k > 1)
        for (std::size_t i = k / 2; i-- > 0;)
          sift_down(i);
      for (std::size_t i = k; i < candidates_.size(); ++i) {
        if (Better{}(candidates_[i], candidates_[0])) {
          std::swap(candidates_[i], candidates_[0]);
          sift_down(0);
        }
      }
    }

    std::size_t beam_width_;
    SelectionPolicy selection_policy_;
    std::size_t batch_limit_;
    Buffer candidates_;
    std::vector<Candidate> finished_candidates_;
    std::uint32_t current_parent_ = kNoParent;
    std::uint32_t local_order_ = 0;
    bool selected_ = false;
    bool finished_sorted_ = true;
  };

 private:
  struct TourEntry {
    int leaf_index;
    std::uint32_t edge_id;
    Action action;
  };

  static constexpr int kForward = -1;
  static constexpr int kBackward = -2;

  // ============================================================
  // 【利用者が StateBase<Selector> に必ず実装する4メソッド】
  //
  //   Score make_initial_node();
  //     - 探索開始時に1回呼ばれる。初期化を行い、初期Scoreを返す。
  //
  //   void expand(int parent, Selector& selector);
  //     - 現在Stateから遷移可能な候補を列挙する。
  //     - selector.push(action, child_score, parent, finished)
  //       または selector.push(action, child_score) を呼ぶ。
  //     - 呼び出し前後でStateの内容を変えてはいけない。
  //
  //   void apply(const Action& action);
  //     - 現在Stateを、そのAction適用後の子ノード状態へ進める。
  //
  //   void undo(const Action& action);
  //     - applyを完全に取り消し、元の親Stateへ戻す。
  //
  // 特に apply -> undo の前後でStateが完全一致することが必須。
  // ============================================================
  template <class S>
  static constexpr bool state_contract = requires(S state, Selector &selector, const Action &action) {
    { state.make_initial_node() } -> std::same_as<Score>;
    { state.expand(0, selector) } -> std::same_as<void>;
    { state.apply(action) } -> std::same_as<void>;
    { state.undo(action) } -> std::same_as<void>;
  };

  class Tree {
   public:
    explicit Tree(const State<Selector> &state, const Config &config)
        : state_(state), selection_policy_(config.selection_policy), lca_switch_ratio_(config.lca_switch_ratio) {
      curr_tour_.reserve(config.tour_capacity);
      next_tour_.reserve(config.tour_capacity);
      direct_road_.reserve(static_cast<std::size_t>(std::max(config.max_turn, 0)));
      counts_.resize(config.beam_width);
      offsets_.resize(config.beam_width + 1);
      positions_.resize(config.beam_width);
      seen_stamp_.resize(config.beam_width, 0u);
      grouped_indices_.reserve(config.beam_width);
      if (config.lca_switch_ratio != 0) {
        if (config.lca_capacity != 0) {
          lca_parent_.reserve(config.lca_capacity + 1);
          lca_edges_.reserve(config.lca_capacity);
        }
        lca_leaves_.reserve(config.beam_width);
        lca_next_.reserve(config.beam_width);
        lca_down_.reserve(static_cast<std::size_t>(std::max(config.max_turn, 0)) + 1);
      }
    }

    void dfs(Selector &selector) {
      if (!initialized_) {
        [[maybe_unused]] const Score initial_score = state_.make_initial_node();
        initialized_ = true;
      }

      if (lca_mode_) {
        lca_dfs(selector);
        return;
      }

      if (curr_tour_.empty()) {
        selector.begin_parent(0);
        state_.expand(0, selector);
        return;
      }

      for (auto it = curr_tour_.begin() + static_cast<std::ptrdiff_t>(active_begin_),
                e = curr_tour_.begin() + static_cast<std::ptrdiff_t>(active_end_);
           it != e; ++it) {
        const TourEntry &entry = *it;
        if (entry.leaf_index >= 0) {
          state_.apply(entry.action);
          selector.begin_parent(entry.leaf_index);
          state_.expand(entry.leaf_index, selector);
          state_.undo(entry.action);
        } else if (entry.leaf_index == kForward) {
          state_.apply(entry.action);
        } else {
          assert(entry.leaf_index == kBackward);
          state_.undo(entry.action);
        }
      }
    }

    void update(std::span<const Candidate> candidates) {
      assert(!candidates.empty());
      if (lca_mode_) {
        lca_update(candidates);
        return;
      }

      if (curr_tour_.empty()) {
        curr_tour_.clear();
        int leaf_index = 0;
        if (selection_policy_ != SelectionPolicy::PartialSort) {
          grouped_indices_.resize(candidates.size());
          for (std::uint32_t i = 0; i < candidates.size(); ++i)
            grouped_indices_[i] = i;
          std::sort(grouped_indices_.begin(), grouped_indices_.end(),
                    [&](std::uint32_t a, std::uint32_t b) { return Selector::better(candidates[a], candidates[b]); });
          for (std::uint32_t idx : grouped_indices_) {
            const Candidate &candidate = candidates[idx];
            curr_tour_.push_back(TourEntry{leaf_index++, next_edge_id_++, candidate.action});
          }
        } else {
          for (const Candidate &candidate : candidates) {
            curr_tour_.push_back(TourEntry{leaf_index++, next_edge_id_++, candidate.action});
          }
        }
        leaf_count_ = leaf_index;
        active_begin_ = 0;
        active_end_ = curr_tour_.size();
        compress_common_prefix();
        maybe_switch_to_lca();
        return;
      }

      const GroupMode group_mode = make_groups(candidates);
      const bool all_survive = group_mode != GroupMode::General;

      const std::size_t first = active_begin_;
      const std::size_t last = active_end_;

      next_tour_.clear();
      int next_leaf_count = 0;

      if (all_survive) {
        // Internal tour entries can never disappear when every old leaf
        // survives. Copy long internal runs in bulk; only leaves need to be
        // rewritten.
        std::size_t run_begin = first;
        for (std::size_t i = first; i < last; ++i) {
          const TourEntry &entry = curr_tour_[i];
          if (entry.leaf_index < 0)
            continue;

          if (run_begin < i) {
            next_tour_.insert(next_tour_.end(), curr_tour_.begin() + static_cast<std::ptrdiff_t>(run_begin),
                              curr_tour_.begin() + static_cast<std::ptrdiff_t>(i));
          }

          const int parent = entry.leaf_index;
          next_tour_.push_back(TourEntry{kForward, entry.edge_id, entry.action});

          if (group_mode == GroupMode::OneChildPerParent) {
            const Candidate &child = candidates[grouped_indices_[static_cast<std::size_t>(parent)]];
            next_tour_.push_back(TourEntry{next_leaf_count++, next_edge_id_++, child.action});
          } else {
            const std::uint32_t child_begin = offsets_[static_cast<std::size_t>(parent)];
            const std::uint32_t child_end = offsets_[static_cast<std::size_t>(parent) + 1];
            for (std::uint32_t j = child_begin; j < child_end; ++j) {
              const Candidate &child = candidates[grouped_indices_[j]];
              next_tour_.push_back(TourEntry{next_leaf_count++, next_edge_id_++, child.action});
            }
          }
          next_tour_.push_back(TourEntry{kBackward, entry.edge_id, entry.action});
          run_begin = i + 1;
        }
        if (run_begin < last) {
          next_tour_.insert(next_tour_.end(), curr_tour_.begin() + static_cast<std::ptrdiff_t>(run_begin),
                            curr_tour_.begin() + static_cast<std::ptrdiff_t>(last));
        }
        leaf_count_ = next_leaf_count;
        curr_tour_.swap(next_tour_);
        active_begin_ = 0;
        active_end_ = curr_tour_.size();
        compress_common_prefix();
        maybe_switch_to_lca();
        return;
      }

      for (std::size_t i = first; i < last; ++i) {
        const TourEntry &entry = curr_tour_[i];
        if (entry.leaf_index >= 0) {
          const int parent = entry.leaf_index;
          assert(0 <= parent && parent < leaf_count_);
          const std::uint32_t child_begin = offsets_[static_cast<std::size_t>(parent)];
          const std::uint32_t child_end = offsets_[static_cast<std::size_t>(parent) + 1];
          if (child_begin == child_end)
            continue;

          next_tour_.push_back(TourEntry{kForward, entry.edge_id, entry.action});
          for (std::uint32_t j = child_begin; j < child_end; ++j) {
            const Candidate &child = candidates[grouped_indices_[j]];
            next_tour_.push_back(TourEntry{next_leaf_count++, next_edge_id_++, child.action});
          }
          next_tour_.push_back(TourEntry{kBackward, entry.edge_id, entry.action});
        } else if (entry.leaf_index == kForward) {
          next_tour_.push_back(entry);
        } else {
          assert(entry.leaf_index == kBackward);
          if (!next_tour_.empty() && next_tour_.back().leaf_index == kForward &&
              next_tour_.back().edge_id == entry.edge_id) {
            next_tour_.pop_back();
          } else {
            next_tour_.push_back(entry);
          }
        }
      }

      leaf_count_ = next_leaf_count;
      curr_tour_.swap(next_tour_);
      active_begin_ = 0;
      active_end_ = curr_tour_.size();
      compress_common_prefix();
      maybe_switch_to_lca();
    }

    std::vector<Action> calculate_path(int parent, int turn) const {
      std::vector<Action> result;
      result.reserve(static_cast<std::size_t>(std::max(turn, 0)));
      for (const Action &action : direct_road_)
        result.push_back(action);

      if (lca_mode_) {
        assert(parent >= 0 && static_cast<std::size_t>(parent) < lca_leaves_.size());
        std::uint32_t u = lca_leaves_[static_cast<std::size_t>(parent)];
        std::vector<Action> suffix;
        suffix.reserve(static_cast<std::size_t>(std::max(turn, 0)));
        while (u != 0) {
          suffix.push_back(lca_edges_[u - 1].action);
          u = lca_parent_[u];
        }
        result.insert(result.end(), suffix.rbegin(), suffix.rend());
        return result;
      }

      if (curr_tour_.empty()) {
        assert(parent == 0);
        return result;
      }

      for (auto it = curr_tour_.begin() + static_cast<std::ptrdiff_t>(active_begin_),
                e = curr_tour_.begin() + static_cast<std::ptrdiff_t>(active_end_);
           it != e; ++it) {
        const TourEntry &entry = *it;
        if (entry.leaf_index >= 0) {
          if (entry.leaf_index == parent) {
            result.push_back(entry.action);
            return result;
          }
        } else if (entry.leaf_index == kForward) {
          result.push_back(entry.action);
        } else {
          assert(entry.leaf_index == kBackward);
          assert(!result.empty());
          result.pop_back();
        }
      }
      assert(false && "parent leaf not found in Euler tour");
      return {};
    }

    // 現在の探索用state_を初期状態までundoし、pathを適用した最終Stateを
    // moveで取り出す。State本体の追加コピーは行わない。
    State<Selector> take_state_at_path(std::span<const Action> path) {
      assert(initialized_);

      // LCA backendではdfs()終了時のstate_が最後に訪れたleaf上に残る。
      // まずLCA root（= direct_road_適用済み状態）まで戻す。
      if (lca_mode_) {
        while (lca_current_ != 0) {
          const auto &edge = lca_edge(lca_current_);
          state_.undo(edge.action);
          lca_current_ = lca_parent_[lca_current_];
        }
      }

      // Euler/LCA共通の圧縮済みprefixを逆順にundoし、
      // make_initial_node()直後の初期状態まで戻す。
      for (auto it = direct_road_.rbegin(); it != direct_road_.rend(); ++it)
        state_.undo(*it);

      // 最良経路を適用して、探索用に保持していた唯一のStateを返す。
      for (const Action &action : path)
        state_.apply(action);

      return std::move(state_);
    }

   private:
    struct LcaEdge {
      Action action;
    };

    const LcaEdge &lca_edge(std::uint32_t node) const {
      assert(node > 0);
      return lca_edges_[node - 1];
    }

    std::uint32_t lca_add(std::uint32_t parent, const Action &action) {
      assert(lca_parent_.size() <= std::numeric_limits<std::uint32_t>::max());
      const auto id = static_cast<std::uint32_t>(lca_parent_.size());
      lca_parent_.push_back(parent);
      lca_edges_.push_back(LcaEdge{action});
      return id;
    }

    void maybe_switch_to_lca() {
      if (lca_mode_ || lca_switch_ratio_ == 0 || leaf_count_ <= 0)
        return;
      const std::size_t active = active_end_ - active_begin_;
      const std::size_t leaves = static_cast<std::size_t>(leaf_count_);
      if (lca_switch_ratio_ > std::numeric_limits<std::size_t>::max() / leaves)
        return;
      if (active <= lca_switch_ratio_ * leaves)
        return;

      lca_parent_.clear();
      lca_edges_.clear();
      lca_leaves_.clear();
      lca_parent_.push_back(0);  // root = currently applied direct-road state
      std::vector<std::uint32_t> stack;
      stack.reserve(static_cast<std::size_t>(leaf_count_));
      stack.push_back(0);
      for (std::size_t i = active_begin_; i < active_end_; ++i) {
        const TourEntry &e = curr_tour_[i];
        if (e.leaf_index == kForward) {
          const auto id = lca_add(stack.back(), e.action);
          stack.push_back(id);
        } else if (e.leaf_index == kBackward) {
          assert(stack.size() > 1);
          stack.pop_back();
        } else {
          lca_leaves_.push_back(lca_add(stack.back(), e.action));
        }
      }
      assert(stack.size() == 1);
      assert(lca_leaves_.size() == static_cast<std::size_t>(leaf_count_));
      lca_current_ = 0;
      lca_reverse_ = false;
      lca_mode_ = true;
      // Flat tour is no longer needed. Keep capacity allocation out of future
      // hot paths.
      curr_tour_.clear();
      next_tour_.clear();
      active_begin_ = active_end_ = 0;
    }

    void lca_move_same_depth(std::uint32_t target) {
      if (lca_current_ == target)
        return;
      lca_down_.clear();
      std::uint32_t u = lca_current_, v = target;
      while (u != v) {
        assert(u != 0 && v != 0);
        const auto &ue = lca_edge(u);
        state_.undo(ue.action);
        u = lca_parent_[u];
        lca_down_.push_back(v);
        v = lca_parent_[v];
      }
      for (auto it = lca_down_.rbegin(); it != lca_down_.rend(); ++it) {
        const auto &e = lca_edge(*it);
        state_.apply(e.action);
      }
      lca_current_ = target;
    }

    void lca_enter_first_child(std::uint32_t target) {
      const std::uint32_t parent = lca_parent_[target];
      lca_move_same_depth(parent);
      const auto &e = lca_edge(target);
      state_.apply(e.action);
      lca_current_ = target;
    }

    void lca_enter_from_root(std::uint32_t target) {
      assert(lca_current_ == 0);
      lca_down_.clear();
      for (std::uint32_t u = target; u != 0; u = lca_parent_[u]) {
        lca_down_.push_back(u);
      }
      for (auto it = lca_down_.rbegin(); it != lca_down_.rend(); ++it) {
        const auto &e = lca_edge(*it);
        state_.apply(e.action);
      }
      lca_current_ = target;
    }

    void lca_dfs(Selector &selector) {
      bool first = true;
      if (!lca_reverse_) {
        for (std::uint32_t i = 0; i < lca_leaves_.size(); ++i) {
          const auto target = lca_leaves_[i];
          if (first && lca_current_ == 0)
            lca_enter_from_root(target);
          else if (first)
            lca_enter_first_child(target);
          else
            lca_move_same_depth(target);
          first = false;
          selector.begin_parent(static_cast<int>(i));
          state_.expand(static_cast<int>(i), selector);
        }
      } else {
        for (std::uint32_t ii = static_cast<std::uint32_t>(lca_leaves_.size()); ii-- > 0;) {
          const auto target = lca_leaves_[ii];
          if (first && lca_current_ == 0)
            lca_enter_from_root(target);
          else if (first)
            lca_enter_first_child(target);
          else
            lca_move_same_depth(target);
          first = false;
          selector.begin_parent(static_cast<int>(ii));
          state_.expand(static_cast<int>(ii), selector);
        }
      }
    }

    void lca_update(std::span<const Candidate> candidates) {
      const GroupMode group_mode = make_groups(candidates);
      lca_next_.clear();
      for (std::uint32_t p = 0; p < lca_leaves_.size(); ++p) {
        const auto parent_node = lca_leaves_[p];
        if (group_mode == GroupMode::OneChildPerParent) {
          const Candidate &c = candidates[grouped_indices_[p]];
          lca_next_.push_back(lca_add(parent_node, c.action));
          continue;
        }
        const std::uint32_t b = offsets_[p], e = offsets_[p + 1];
        for (std::uint32_t q = b; q < e; ++q) {
          const Candidate &c = candidates[grouped_indices_[q]];
          lca_next_.push_back(lca_add(parent_node, c.action));
        }
      }
      lca_leaves_.swap(lca_next_);
      leaf_count_ = static_cast<int>(lca_leaves_.size());
      lca_reverse_ = !lca_reverse_;
    }

    void compress_common_prefix() {
      while (active_begin_ < active_end_ && curr_tour_[active_begin_].leaf_index == kForward &&
             curr_tour_[active_end_ - 1].leaf_index == kBackward &&
             curr_tour_[active_begin_].edge_id == curr_tour_[active_end_ - 1].edge_id) {
        const TourEntry &entry = curr_tour_[active_begin_];
        state_.apply(entry.action);
        direct_road_.push_back(entry.action);
        ++active_begin_;
        --active_end_;
      }
    }

    enum class GroupMode : std::uint8_t {
      General,
      AllSurvive,
      OneChildPerParent,
    };

    GroupMode make_groups(std::span<const Candidate> candidates) {
      assert(leaf_count_ >= 0);
      assert(static_cast<std::size_t>(leaf_count_) <= counts_.size());

      // Fast permutation check for the common deep/spread case.
      if (candidates.size() == static_cast<std::size_t>(leaf_count_)) {
        if (++stamp_epoch_ == 0) {
          std::fill(seen_stamp_.begin(), seen_stamp_.end(), 0u);
          stamp_epoch_ = 1;
        }
        grouped_indices_.resize(candidates.size());
        bool unique = true;
        for (std::uint32_t i = 0; i < candidates.size(); ++i) {
          const std::uint32_t parent = candidates[i].parent();
          assert(parent < static_cast<std::uint32_t>(leaf_count_));
          if (seen_stamp_[parent] == stamp_epoch_) {
            unique = false;
            break;
          }
          seen_stamp_[parent] = stamp_epoch_;
          grouped_indices_[parent] = i;
        }
        if (unique)
          return GroupMode::OneChildPerParent;
      }

      std::fill(counts_.begin(), counts_.begin() + leaf_count_, 0u);
      std::uint32_t surviving_parents = 0;
      for (const Candidate &candidate : candidates) {
        assert(candidate.parent() < static_cast<std::uint32_t>(leaf_count_));
        auto &count = counts_[static_cast<std::size_t>(candidate.parent())];
        if (count++ == 0)
          ++surviving_parents;
      }

      offsets_[0] = 0;
      for (int i = 0; i < leaf_count_; ++i) {
        offsets_[static_cast<std::size_t>(i) + 1] =
            offsets_[static_cast<std::size_t>(i)] + counts_[static_cast<std::size_t>(i)];
      }

      std::copy(offsets_.begin(), offsets_.begin() + leaf_count_, positions_.begin());
      grouped_indices_.resize(candidates.size());

      for (std::uint32_t i = 0; i < candidates.size(); ++i) {
        const std::uint32_t parent = candidates[i].parent();
        grouped_indices_[positions_[static_cast<std::size_t>(parent)]++] = i;
      }

      if (selection_policy_ != SelectionPolicy::PartialSort) {
        for (int parent = 0; parent < leaf_count_; ++parent) {
          const auto begin = grouped_indices_.begin() + offsets_[static_cast<std::size_t>(parent)];
          const auto end = grouped_indices_.begin() + offsets_[static_cast<std::size_t>(parent) + 1];
          const auto len = end - begin;
          if (len > 1 && len <= 4) {
            for (auto it = begin + 1; it != end; ++it) {
              const std::uint32_t x = *it;
              auto jt = it;
              while (jt != begin && Selector::better(candidates[x], candidates[*(jt - 1)])) {
                *jt = *(jt - 1);
                --jt;
              }
              *jt = x;
            }
          } else if (len > 1) {
            std::sort(begin, end,
                      [&](std::uint32_t a, std::uint32_t b) { return Selector::better(candidates[a], candidates[b]); });
          }
        }
      }
      return surviving_parents == static_cast<std::uint32_t>(leaf_count_) ? GroupMode::AllSurvive : GroupMode::General;
    }

    State<Selector> state_;
    SelectionPolicy selection_policy_;
    bool initialized_ = false;

    std::vector<TourEntry> curr_tour_;
    std::vector<TourEntry> next_tour_;
    std::vector<Action> direct_road_;

    std::vector<std::uint32_t> counts_;
    std::vector<std::uint32_t> offsets_;
    std::vector<std::uint32_t> positions_;
    std::vector<std::uint32_t> seen_stamp_;
    std::uint32_t stamp_epoch_ = 0;
    std::vector<std::uint32_t> grouped_indices_;

    std::size_t lca_switch_ratio_ = 0;
    bool lca_mode_ = false;
    bool lca_reverse_ = false;
    std::vector<std::uint32_t> lca_parent_;
    std::vector<LcaEdge> lca_edges_;
    std::vector<std::uint32_t> lca_leaves_, lca_next_, lca_down_;
    std::uint32_t lca_current_ = 0;

    int leaf_count_ = 0;
    std::size_t active_begin_ = 0;
    std::size_t active_end_ = 0;
    std::uint32_t next_edge_id_ = 0;
  };

 public:
  static_assert(state_contract<State<Selector>>,
                "State<Selector> は make_initial_node(), expand(parent, selector), "
                "apply(action), undo(action) を実装してください");
  static_assert(std::copy_constructible<State<Selector>>,
                "State<Selector> は copy constructible "
                "である必要があります（探索開始時に1回だけコピーします）");

  State<Selector> beam_search(const Config &config, const State<Selector> &state) const {
    assert(config.max_turn >= 0);
    assert(config.beam_width > 0);
    assert(config.batch_factor >= 2);
    if constexpr (FixedCandidateBuffer) {
      if (config.candidate_capacity == 0)
        std::abort();
    }

    // max_turn==0では探索自体を開始しないためmake_initial_node()も呼ばない。
    // const参照の入力Stateをそのまま返すため、この場合のコピーは1回だけ。
    if (config.max_turn == 0)
      return state;

    // State本体のコピーはTree構築時のこの1回だけ。以降はapply/undoで使い回し、
    // 最後はTree内のstate_をmoveして返す。
    Tree tree(state, config);
    Selector selector(config);

    std::optional<Score> best_finished_score;
    std::vector<Action> best_finished_path;

    for (int turn = 0; turn < config.max_turn; ++turn) {
      tree.dfs(selector);

      if (selector.have_finished()) {
        const auto &finished = selector.get_finished_candidates();
        for (const Candidate &candidate : finished) {
          if (!best_finished_score || candidate.score < *best_finished_score) {
            auto path = tree.calculate_path(static_cast<int>(candidate.parent()), turn + 1);
            path.push_back(candidate.action);
            best_finished_score = candidate.score;
            best_finished_path = std::move(path);
          }
        }
        if (config.return_finished_immediately)
          return tree.take_state_at_path(best_finished_path);
        selector.clear_finished_candidates();
      }

      const auto &selected = selector.select();
      if (selected.empty())
        return tree.take_state_at_path(best_finished_path);

      if (turn == config.max_turn - 1) {
        const Candidate &best = selector.calculate_best_candidate();
        auto path = tree.calculate_path(static_cast<int>(best.parent()), turn + 1);
        path.push_back(best.action);
        return tree.take_state_at_path(path);
      }

      tree.update(selected);
      selector.clear();
    }

    std::abort();  // config.max_turn > 0 なら到達しない。
  }
};

}  // namespace etbs