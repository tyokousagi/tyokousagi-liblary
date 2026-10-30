// I love hina-no.
// tyokousagi-FixedVector
//
// 容量を固定した vector。要素を自分の中に持ち、ヒープ確保をしない。
// - State に持たせても、コピーは使っている分（size 個）だけで済む
// - vector と違い、push_back で再確保が起きない
// 容量を超えると assert で止まる（-DNDEBUG なしのとき）。
//
// 使い方:
//   FixedVector<int, 100> v;
//   v.push_back(3);
//   v.pop_back();
//   for (int x : v) { ... }
//   v.swap_remove(i);  // i 番目を末尾の要素で上書きして消す。O(1)、順番は変わる
//   v.erase(i);        // i 番目を消して後ろを詰める。O(size)、順番は保つ
//   v.insert(i, x);    // i 番目に x を入れる。O(size)

#include <algorithm>
#include <array>
#include <cassert>
#include <utility>

template <class T, int CAP>
struct FixedVector {
  std::array<T, CAP> buf;  // 先頭 n 個だけが有効
  int n = 0;

  FixedVector() = default;
  FixedVector(const FixedVector &other) : n(other.n) {
    std::copy_n(other.buf.begin(), n, buf.begin());
  }
  FixedVector &operator=(const FixedVector &other) {
    n = other.n;
    std::copy_n(other.buf.begin(), n, buf.begin());
    return *this;
  }

  static constexpr int capacity() {
    return CAP;
  }
  int size() const {
    return n;
  }
  bool empty() const {
    return n == 0;
  }
  bool full() const {
    return n == CAP;
  }

  void push_back(const T &x) {
    assert(n < CAP);
    buf[n++] = x;
  }
  template <class... Args>
  T &emplace_back(Args &&...args) {
    assert(n < CAP);
    buf[n] = T(std::forward<Args>(args)...);
    return buf[n++];
  }
  void pop_back() {
    assert(n > 0);
    --n;
  }
  void clear() {
    n = 0;
  }
  // 大きさを m にする。増えた分は x で埋める
  void resize(int m, const T &x = T()) {
    assert(0 <= m && m <= CAP);
    if (m > n) {
      std::fill(buf.begin() + n, buf.begin() + m, x);
    }
    n = m;
  }

  // i 番目を末尾の要素で上書きして消す。O(1)、順番は変わる
  void swap_remove(int i) {
    assert(0 <= i && i < n);
    buf[i] = buf[n - 1];
    --n;
  }
  // i 番目を消して後ろを詰める。O(size)
  void erase(int i) {
    assert(0 <= i && i < n);
    std::copy(buf.begin() + i + 1, buf.begin() + n, buf.begin() + i);
    --n;
  }
  // i 番目に x を入れる（0 <= i <= size）。O(size)
  void insert(int i, const T &x) {
    assert(0 <= i && i <= n && n < CAP);
    std::copy_backward(buf.begin() + i, buf.begin() + n, buf.begin() + n + 1);
    buf[i] = x;
    ++n;
  }

  T &operator[](int i) {
    assert(0 <= i && i < n);
    return buf[i];
  }
  const T &operator[](int i) const {
    assert(0 <= i && i < n);
    return buf[i];
  }
  T &front() {
    assert(n > 0);
    return buf[0];
  }
  const T &front() const {
    assert(n > 0);
    return buf[0];
  }
  T &back() {
    assert(n > 0);
    return buf[n - 1];
  }
  const T &back() const {
    assert(n > 0);
    return buf[n - 1];
  }

  T *begin() {
    return buf.data();
  }
  const T *begin() const {
    return buf.data();
  }
  T *end() {
    return buf.data() + n;
  }
  const T *end() const {
    return buf.data() + n;
  }

  bool operator==(const FixedVector &other) const {
    return n == other.n && std::equal(begin(), end(), other.begin());
  }
  bool operator!=(const FixedVector &other) const {
    return !(*this == other);
  }
};
