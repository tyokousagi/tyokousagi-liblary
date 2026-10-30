#!/usr/bin/env python3
"""pahcer の結果を seed ごとに比較する。

pahcer は平均スコアや相対スコアの推移は出してくれるが、
2つの結果を seed ごとに見比べたり、入力パラメータ別に良くなった / 悪くなったを
見る機能はない。このスクリプトはそれを補う。
コンテストのディレクトリ（pahcer_config.toml がある場所）で実行する。

使い方:
  python3 compare.py                     # 最新の結果と、その1つ前を比較
  python3 compare.py -b best             # 最新の結果と、各 seed のこれまでのベストを比較
  python3 compare.py -b -3 -t -1         # 3つ前と最新を比較（-1 が最新）
  python3 compare.py -b 20261017_160458  # ファイル名の一部・タグ名・コメントでも指定できる
  python3 compare.py --params N,M,_,T    # 入力1行目の値ごとに集計（_ の列は使わない）
"""

import argparse
import glob
import json
import math
import os
import re
import sys
import unicodedata
from dataclasses import dataclass, field


@dataclass
class Run:
    label: str
    scores: dict  # seed -> score
    times: dict = field(default_factory=dict)  # seed -> 実行時間[s]（best では空）
    errors: set = field(default_factory=set)  # エラーになった seed


def read_config(contest_dir):
    """pahcer_config.toml から目的（最大化か）と結果の出力先を読む。"""
    maximize, out_dir = True, "./pahcer"
    path = os.path.join(contest_dir, "pahcer_config.toml")
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            text = f.read()
        m = re.search(r'^\s*objective\s*=\s*"(\w+)"', text, re.M)
        if m:
            maximize = m.group(1).lower() == "max"
        m = re.search(r'^\s*out_dir\s*=\s*"([^"]+)"', text, re.M)
        if m:
            out_dir = m.group(1)
    return maximize, os.path.join(contest_dir, out_dir)


def load_result(path):
    with open(path, encoding="utf-8") as f:
        d = json.load(f)
    cases = d["cases"]
    label = os.path.basename(path)
    if d.get("tag_name"):
        label += f" [{d['tag_name']}]"
    if d.get("comment"):
        label += f' "{d["comment"]}"'
    errors = {c["seed"] for c in cases if c.get("error_message")}
    errors |= set(d.get("wa_seeds") or [])
    return Run(
        label=label,
        scores={c["seed"]: c["score"] for c in cases},
        times={c["seed"]: c["execution_time"] for c in cases},
        errors=errors,
    )


def load_best(pahcer_dir):
    path = os.path.join(pahcer_dir, "best_scores.json")
    with open(path, encoding="utf-8") as f:
        d = json.load(f)
    return Run(label="best_scores.json", scores={int(k): v for k, v in d.items()})


def resolve(spec, pahcer_dir, result_paths):
    if spec == "best":
        return load_best(pahcer_dir)
    if re.fullmatch(r"-\d+", spec):
        i = int(spec)
        if -i > len(result_paths):
            sys.exit(f"結果は {len(result_paths)} 件しかありません: {spec}")
        return load_result(result_paths[i])
    if os.path.exists(spec):
        return load_result(spec)
    # ファイル名の一部・タグ名・コメントで探す（新しいものを優先）
    for path in reversed(result_paths):
        if spec in os.path.basename(path):
            return load_result(path)
        with open(path, encoding="utf-8") as f:
            d = json.load(f)
        if spec in (d.get("tag_name"), d.get("comment")):
            return load_result(path)
    sys.exit(f"結果が見つかりません: {spec}")


def width(s):
    return sum(2 if unicodedata.east_asian_width(ch) in "FW" else 1 for ch in s)


def ljust(s, w):
    return s + " " * max(0, w - width(s))


def rjust(s, w):
    return " " * max(0, w - width(s)) + s


def pct(x):
    return f"{x * 100:+.2f}%"


def gmean_ratio(log_ratios):
    return math.exp(sum(log_ratios) / len(log_ratios)) - 1 if log_ratios else float("nan")


def read_params(in_dir, seeds, names):
    """各 seed の入力1行目から、名前を付けた列の値を読む。"""
    params = {}
    for seed in seeds:
        path = os.path.join(in_dir, f"{seed:04}.txt")
        if not os.path.exists(path):
            print(f"入力ファイルがないのでパラメータ別の集計をしません: {path}")
            return None
        with open(path, encoding="utf-8") as f:
            tokens = f.readline().split()
        values = {}
        for name, tok in zip(names, tokens):
            if name == "_":
                continue
            try:
                values[name] = int(tok)
            except ValueError:
                values[name] = float(tok)
        params[seed] = values
    return params


def make_bins(seed_values, bins):
    """(seed, 値) の列を、値の小さい順におよそ同じ件数の bins 個のグループに分ける。同じ値は同じグループに入れる。"""
    items = sorted(seed_values, key=lambda x: x[1])
    distinct = sorted({v for _, v in items})
    if len(distinct) <= bins:
        return [[s for s, v in items if v == d] for d in distinct], [(d, d) for d in distinct]
    groups, ranges = [], []
    i, n = 0, len(items)
    for b in range(bins):
        end = max(i + 1, round(n * (b + 1) / bins))
        while end < n and items[end][1] == items[end - 1][1]:
            end += 1
        if i >= n:
            break
        chunk = items[i:end]
        groups.append([s for s, _ in chunk])
        ranges.append((chunk[0][1], chunk[-1][1]))
        i = end
    return groups, ranges


def main():
    parser = argparse.ArgumentParser(
        description="pahcer の結果を seed ごとに比較する", formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("-d", "--dir", default=".", help="コンテストのディレクトリ（既定: カレント）")
    parser.add_argument("-b", "--base", default="-2", help="比較元（既定: -2 = 1つ前の結果）。best も指定できる")
    parser.add_argument("-t", "--target", default="-1", help="比較先（既定: -1 = 最新の結果）")
    parser.add_argument("-n", "--top", type=int, default=5, help="悪化 / 改善した seed を何件表示するか")
    parser.add_argument("-p", "--params", help="入力1行目の各値の名前をカンマ区切りで。例: N,M,_,T")
    parser.add_argument("--bins", type=int, default=4, help="パラメータ別集計の分割数")
    parser.add_argument("--in-dir", help="入力ファイルのディレクトリ（既定: <dir>/tools/in）")
    args = parser.parse_args()

    maximize, pahcer_dir = read_config(args.dir)
    result_paths = sorted(glob.glob(os.path.join(pahcer_dir, "json", "result_*.json")))
    if not result_paths:
        sys.exit(f"pahcer の結果が見つかりません: {os.path.join(pahcer_dir, 'json')}")
    if len(result_paths) == 1 and args.base == "-2":
        sys.exit("結果が1つしかありません。-b best でベストと比較するか、もう一度 pahcer run してください")

    base = resolve(args.base, pahcer_dir, result_paths)
    target = resolve(args.target, pahcer_dir, result_paths)
    common = sorted(set(base.scores) & set(target.scores))
    # エラーの seed は pahcer では 0 点になる。最小化だと「改善」に見えてしまうので比較から除く
    error_seeds = [s for s in common if s in base.errors or s in target.errors]
    seeds = [s for s in common if s not in base.errors and s not in target.errors]
    if not seeds:
        sys.exit("比較できる seed がありません")

    def better(a, b):
        return a > b if maximize else a < b

    # seed ごとの比。+ が改善になるよう、最小化では逆数を取る
    log_ratio = {}
    win = lose = draw = 0
    for s in seeds:
        b, t = base.scores[s], target.scores[s]
        if better(t, b):
            win += 1
        elif better(b, t):
            lose += 1
        else:
            draw += 1
        if b > 0 and t > 0:
            log_ratio[s] = math.log(t / b) if maximize else math.log(b / t)
    zero_seeds = [s for s in seeds if s not in log_ratio]

    print(f"base  : {base.label}")
    print(f"target: {target.label}")
    print(f"目的  : {'最大化' if maximize else '最小化'}   比較した seed: {len(seeds)} 件   （比は + が改善）")
    if error_seeds:
        print(f"エラーのため比較から除いた seed: {' '.join(f'{s:04}' for s in error_seeds)}")
    print()

    mean_b = sum(base.scores[s] for s in seeds) / len(seeds)
    mean_t = sum(target.scores[s] for s in seeds) / len(seeds)
    mean_change = (mean_t / mean_b if maximize else mean_b / mean_t) - 1 if mean_b > 0 and mean_t > 0 else float("nan")
    rows = [("平均スコア", f"{mean_b:,.2f}", f"{mean_t:,.2f}", pct(mean_change))]
    if base.times or target.times:
        fmt_time = lambda run: f"{max(run.times[s] for s in common):.3f}s" if run.times else "-"
        rows.append(("最大実行時間", fmt_time(base), fmt_time(target), ""))
    count_err = lambda run: str(len(run.errors & set(common))) if run.times else "-"
    rows.append(("エラー", count_err(base), count_err(target), ""))
    print(ljust("", 14) + rjust("base", 16) + rjust("target", 16))
    for name, b, t, extra in rows:
        print(ljust(name, 14) + rjust(b, 16) + rjust(t, 16) + ("   " + extra if extra else ""))
    print()
    print(f"seed ごとの比（幾何平均）: {pct(gmean_ratio(list(log_ratio.values())))}")
    print(f"勝ち / 負け / 引き分け   : {win} / {lose} / {draw}")
    if zero_seeds:
        print(f"どちらかが 0 点で比を計算しなかった seed: {' '.join(f'{s:04}' for s in zero_seeds)}")

    def print_seed_table(title, items):
        if not items:
            return
        print()
        print(title)
        print("  " + ljust("seed", 6) + rjust("base", 14) + rjust("target", 14) + rjust("比", 10))
        for s, lr in items:
            print(
                "  "
                + ljust(f"{s:04}", 6)
                + rjust(f"{base.scores[s]:,}", 14)
                + rjust(f"{target.scores[s]:,}", 14)
                + rjust(pct(math.exp(lr) - 1), 10)
            )

    if args.top > 0:
        ordered = sorted(log_ratio.items(), key=lambda x: x[1])
        print_seed_table(f"悪くなった seed（上位 {args.top} 件）", [x for x in ordered[: args.top] if x[1] < 0])
        print_seed_table(f"良くなった seed（上位 {args.top} 件）", [x for x in reversed(ordered[-args.top :]) if x[1] > 0])

    if args.params:
        names = [x.strip() for x in args.params.split(",")]
        in_dir = args.in_dir or os.path.join(args.dir, "tools", "in")
        params = read_params(in_dir, seeds, names)
        if params is None:
            return
        for name in names:
            if name == "_":
                continue
            seed_values = [(s, params[s][name]) for s in seeds if name in params[s]]
            if not seed_values:
                print(f"\n{name}: 入力1行目にこの列がありません")
                continue
            groups, ranges = make_bins(seed_values, args.bins)
            print()
            print(f"[{name}] 別")
            print("  " + ljust("範囲", 20) + rjust("件数", 6) + rjust("比", 10) + rjust("勝/負", 10))
            for group, (lo, hi) in zip(groups, ranges):
                rng = f"{lo}" if lo == hi else f"{lo} 〜 {hi}"
                g_win = sum(better(target.scores[s], base.scores[s]) for s in group)
                g_lose = sum(better(base.scores[s], target.scores[s]) for s in group)
                g_ratio = gmean_ratio([log_ratio[s] for s in group if s in log_ratio])
                print(
                    "  " + ljust(rng, 20) + rjust(str(len(group)), 6) + rjust(pct(g_ratio), 10) + rjust(f"{g_win}/{g_lose}", 10)
                )


if __name__ == "__main__":
    main()
