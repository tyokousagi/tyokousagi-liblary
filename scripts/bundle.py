#!/usr/bin/env python3
"""#include "..." を展開して、提出用の1ファイルにまとめる。

main.cpp ではライブラリをコピペせず、
  #include "../liblary/grid.cpp"
のように読み込んでおく（ローカルではそのままコンパイル・pahcer で実行できる）。
提出前にこのスクリプトで展開する。

- 展開するのは #include "..." だけ。<bits/stdc++.h> や <atcoder/all> などの <...> はそのまま残す
- 同じファイルは1回だけ展開する（ライブラリにインクルードガードがなくてもよい）
- ブロックコメント /* ... */ の中の #include は展開しない
- #pragma once は消す

使い方:
  python3 bundle.py main.cpp > submit.cpp
  python3 bundle.py main.cpp -o submit.cpp --check   # 展開後にコンパイルが通るかも確かめる
  python3 bundle.py main.cpp -I ~/projects/ahc/liblary  # #include "grid.cpp" のように書く場合
"""

import argparse
import os
import re
import subprocess
import sys

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*"([^"]+)"')
PRAGMA_ONCE_RE = re.compile(r"^\s*#\s*pragma\s+once\b")


def update_block_comment(line, in_block):
    """行末の時点でブロックコメントの中にいるかを返す（文字列リテラル中の /* は考慮しない）。"""
    i = 0
    while i < len(line) - 1:
        two = line[i : i + 2]
        if in_block:
            if two == "*/":
                in_block = False
                i += 2
                continue
        else:
            if two == "//":
                break
            if two == "/*":
                in_block = True
                i += 2
                continue
        i += 1
    return in_block


def resolve(name, current_dir, include_dirs):
    for d in [current_dir] + include_dirs:
        path = os.path.join(d, name)
        if os.path.isfile(path):
            return path
    return None


def expand(path, include_dirs, seen, out):
    real = os.path.realpath(path)
    if real in seen:
        return
    seen.add(real)
    current_dir = os.path.dirname(real)
    in_block = False
    with open(path, encoding="utf-8") as f:
        for line in f:
            if not in_block:
                m = INCLUDE_RE.match(line)
                if m:
                    target = resolve(m.group(1), current_dir, include_dirs)
                    if target is None:
                        print(f"警告: {m.group(1)} が見つからないので、そのまま残します（{path}）", file=sys.stderr)
                        out.append(line)
                    elif os.path.realpath(target) not in seen:
                        name = os.path.basename(target)
                        out.append(f"// ---- begin: {name} ----\n")
                        expand(target, include_dirs, seen, out)
                        out.append(f"// ---- end: {name} ----\n")
                    continue
                if PRAGMA_ONCE_RE.match(line):
                    continue
            out.append(line if line.endswith("\n") else line + "\n")
            in_block = update_block_comment(line, in_block)


def main():
    parser = argparse.ArgumentParser(description='#include "..." を展開して提出用の1ファイルにまとめる')
    parser.add_argument("source", help="展開するファイル（例: main.cpp）")
    parser.add_argument("-o", "--output", help="出力先（省略すると標準出力）")
    parser.add_argument("-I", dest="include_dirs", action="append", default=[], help="#include を探すディレクトリ")
    parser.add_argument("--check", action="store_true", help="展開後のコードを g++ でコンパイルできるか確かめる")
    parser.add_argument("--cxx-flags", default="-std=gnu++20 -fsyntax-only", help="--check のときの g++ のオプション")
    args = parser.parse_args()

    out = []
    expand(args.source, [os.path.expanduser(d) for d in args.include_dirs], set(), out)
    code = "".join(out)

    if args.output:
        with open(args.output, "w", encoding="utf-8") as f:
            f.write(code)
        print(f"{args.output} に書き出しました（{len(code.encode('utf-8')):,} バイト）", file=sys.stderr)
    else:
        sys.stdout.write(code)

    if args.check:
        cmd = ["g++", *args.cxx_flags.split(), "-x", "c++", "-", "-o", os.devnull]
        result = subprocess.run(cmd, input=code, text=True)
        if result.returncode != 0:
            sys.exit("コンパイルに失敗しました")
        print("コンパイルできました", file=sys.stderr)


if __name__ == "__main__":
    main()
