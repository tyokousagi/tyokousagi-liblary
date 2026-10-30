// I love hina-no.
// tyokousagi-IndexSet
//
// 0 以上 n 未満の整数の集合。
// 追加・削除・存在判定・ランダムな要素の取得がすべて O(1)。
// 「今いじれる場所」の集合を持っておき、近傍でその中からランダムに1つ選ぶ、といった用途で使う。
//
// 使い方:
//   IndexSet st(N);
//   st.insert(3);
//   st.erase(3);
//   if (st.contains(x)) { ... }
//   int x = st.random(rnd);  // ランダムに1つ（空でないこと）。rnd は rand_int(l, r) を持つ乱数
//   for (int x : st) { ... }  // 列挙（順番は不定）
//
// 注意:
//   erase すると末尾の要素が空いた位置へ移動するので、列挙中に insert / erase しないこと。

#include <cassert>
#include <vector>

struct IndexSet {
  std::vector<int> elems;  // 要素の列（順番は不定）
  std::vector<int> pos;    // pos[x] = elems 内での x の位置。含まれないなら -1

  explicit IndexSet(int n = 0) : pos(n, -1) {
  }

  // 追加する。既に含まれていたら false
  bool insert(int x) {
    assert(0 <= x && x < (int)pos.size());
    if (pos[x] != -1) {
      return false;
    }
    pos[x] = (int)elems.size();
    elems.push_back(x);
    return true;
  }

  // 削除する。含まれていなければ false
  bool erase(int x) {
    assert(0 <= x && x < (int)pos.size());
    int p = pos[x];
    if (p == -1) {
      return false;
    }
    int last = elems.back();
    elems[p] = last;
    pos[last] = p;
    elems.pop_back();
    pos[x] = -1;
    return true;
  }

  bool contains(int x) const {
    assert(0 <= x && x < (int)pos.size());
    return pos[x] != -1;
  }

  int size() const {
    return (int)elems.size();
  }

  bool empty() const {
    return elems.empty();
  }

  // 全要素を削除する。O(size)
  void clear() {
    for (int x : elems) {
      pos[x] = -1;
    }
    elems.clear();
  }

  // i 番目の要素（順番は不定）
  int operator[](int i) const {
    return elems[i];
  }

  template <class Rng>
  int random(Rng &rng) const {
    assert(!elems.empty());
    return elems[rng.rand_int(0, size())];
  }

  std::vector<int>::const_iterator begin() const {
    return elems.begin();
  }
  std::vector<int>::const_iterator end() const {
    return elems.end();
  }
};
