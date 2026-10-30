// I love hina-no.
// tyokousagi-Zobrist hash
//
// 「位置 i に値 v がある」ことに 64bit 乱数を割り当て、状態のハッシュ値をそれらの XOR で表す。
// 1か所を書き換えるたびに hash ^= z(i, 旧値) ^ z(i, 新値) で差分更新できる（O(1)）。
//
// 用途:
//   - ビームサーチの重複除去（beamsearch.cpp の DedupEulerTourBeamSearch / push_with_hash）
//   - 焼きなましやDFSで、同じ状態に戻ったかどうかの検出
//
// 使い方:
//   Zobrist z(N * N, C);  // 位置の数, 値の種類数
//   std::uint64_t hash = 0;
//   for (int i = 0; i < N * N; ++i) hash ^= z(i, board[i]);  // 最初に1回だけ全体から計算
//
//   // board[i] を c に書き換えるとき
//   hash ^= z(i, board[i]) ^ z(i, c);
//   board[i] = c;
//
//   // プレイヤーの位置など「値」がないものは値の種類数 1 で作り、z(pos, 0) を使う
//   Zobrist zp(N * N, 1);
//
// 注意:
//   apply / undo で同じ XOR を2回すると元に戻るので、undo では apply と同じ XOR をもう一度すればよい。

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

struct Zobrist {
  int position_count;
  int value_count;
  std::vector<std::uint64_t> table;

  Zobrist(int position_count, int value_count, std::uint64_t seed = 0x5eed1234abcdULL)
      : position_count(position_count), value_count(value_count), table((std::size_t)position_count * value_count) {
    // splitmix64
    std::uint64_t x = seed;
    for (auto &h : table) {
      std::uint64_t z = (x += 0x9e3779b97f4a7c15ULL);
      z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
      z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
      h = z ^ (z >> 31);
    }
  }

  std::uint64_t operator()(int position, int value) const {
    assert(0 <= position && position < position_count);
    assert(0 <= value && value < value_count);
    return table[(std::size_t)position * value_count + value];
  }
};
