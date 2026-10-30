#!/usr/bin/env python3
"""焼きなましの推移ログ（annealing.cpp の SimulatedAnnealing が書く CSV）をグラフにする。

ログの取り方:
  AHC_SA_TRACE=trace.csv ./a.out < tools/in/0000.txt > out.txt

使い方:
  python3 plot_sa.py trace.csv              # trace.png に描く
  python3 plot_sa.py trace.csv -o sa.png
  python3 plot_sa.py trace.csv --negate     # スコアの符号を反転して表示（最小化で -cost を渡したとき）

上から スコア（と最良スコア）、温度、採用率 を同じ時間軸で並べる。
多点スタートで複数回焼きなました場合は、区切りに縦線を引く。
"""

import argparse
import csv
import math
import os
import sys

try:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
except ImportError:
    sys.exit("matplotlib がありません: pip install matplotlib")

SURFACE = "#fcfcfb"
TEXT = "#0b0b0b"
TEXT_SECONDARY = "#52514e"
GRID = "#e5e4e0"
SERIES_1 = "#2a78d6"  # スコア / 温度 / 採用率
SERIES_2 = "#eb6834"  # 最良スコア
LINE_WIDTH = 1.3


def to_float(s):
    return float(s) if s != "" else math.nan


def load(path):
    rows = []
    with open(path, encoding="utf-8") as f:
        for r in csv.DictReader(f):
            rows.append(
                {
                    "run": int(r["run"]),
                    "time": float(r["time_ms"]),
                    "iter": int(r["iter"]),
                    "temp": float(r["temp"]),
                    "accept": to_float(r["accept_rate"]),
                    "score": to_float(r["score"]),
                    "best": to_float(r["best"]),
                }
            )
    if not rows:
        sys.exit(f"{path} にデータがありません")
    return rows


def style_axis(ax):
    ax.set_facecolor(SURFACE)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(GRID)
    ax.tick_params(colors=TEXT_SECONDARY, labelsize=8)
    ax.yaxis.label.set_color(TEXT_SECONDARY)
    ax.grid(axis="y", color=GRID, linewidth=0.6)
    ax.set_axisbelow(True)


def main():
    parser = argparse.ArgumentParser(description="焼きなましの推移ログをグラフにする")
    parser.add_argument("csv", help="AHC_SA_TRACE で書き出した CSV")
    parser.add_argument("-o", "--output", help="出力する画像（既定: CSV と同じ名前の .png）")
    parser.add_argument("--negate", action="store_true", help="スコアの符号を反転して表示する")
    args = parser.parse_args()

    rows = load(args.csv)
    sign = -1 if args.negate else 1
    runs = sorted({r["run"] for r in rows})

    # 実行ごとのまとめ
    for run in runs:
        rs = [r for r in rows if r["run"] == run]
        last = rs[-1]
        line = f"run {run}: {rs[0]['time']:.1f}〜{last['time']:.1f}ms  反復 {last['iter']:,}"
        if not math.isnan(last["score"]):
            line += f"  最終スコア {sign * last['score']:,.6g}  最良 {sign * last['best']:,.6g}"
        print(line)

    fig, (ax_score, ax_temp, ax_acc) = plt.subplots(3, 1, sharex=True, figsize=(10, 7.5), dpi=150)
    fig.patch.set_facecolor(SURFACE)
    for ax in (ax_score, ax_temp, ax_acc):
        style_axis(ax)

    has_score = any(not math.isnan(r["score"]) for r in rows)
    for run in runs:
        rs = [r for r in rows if r["run"] == run]
        t = [r["time"] for r in rs]
        first = run == runs[0]
        if has_score:
            ax_score.plot(
                t, [sign * r["score"] for r in rs], color=SERIES_1, lw=LINE_WIDTH, label="score" if first else None
            )
            ax_score.plot(
                t,
                [sign * r["best"] for r in rs],
                color=SERIES_2,
                lw=LINE_WIDTH,
                drawstyle="steps-post",
                label="best" if first else None,
            )
        ax_temp.plot(t, [r["temp"] for r in rs], color=SERIES_1, lw=LINE_WIDTH)
        ax_acc.plot(t, [r["accept"] * 100 for r in rs], color=SERIES_1, lw=LINE_WIDTH)

    # 多点スタートの区切り
    if len(runs) > 1:
        for run in runs[1:]:
            t0 = min(r["time"] for r in rows if r["run"] == run)
            for ax in (ax_score, ax_temp, ax_acc):
                ax.axvline(t0, color=TEXT_SECONDARY, lw=0.8, ls=(0, (3, 3)), alpha=0.6)
            ax_score.annotate(
                f"run {run}",
                (t0, 1),
                xycoords=("data", "axes fraction"),
                xytext=(3, -2),
                textcoords="offset points",
                va="top",
                fontsize=7,
                color=TEXT_SECONDARY,
            )

    if has_score:
        ax_score.legend(loc="upper left", frameon=False, fontsize=8, labelcolor=TEXT_SECONDARY)
    else:
        ax_score.text(
            0.5,
            0.5,
            "no score recorded (call sa.next(score) instead of sa.next())",
            transform=ax_score.transAxes,
            ha="center",
            va="center",
            color=TEXT_SECONDARY,
            fontsize=9,
        )
        ax_score.set_yticks([])
    ax_score.set_ylabel("score (negated)" if args.negate else "score")
    if all(r["temp"] > 0 for r in rows):
        ax_temp.set_yscale("log")
    ax_temp.set_ylabel("temperature")
    accept_max = max((r["accept"] * 100 for r in rows if not math.isnan(r["accept"])), default=100)
    ax_acc.set_ylim(0, min(100, accept_max * 1.1) or 1)
    ax_acc.set_ylabel("acceptance rate [%]")
    ax_acc.set_xlabel("time [ms]", color=TEXT_SECONDARY)

    fig.suptitle(f"SA trace: {os.path.basename(args.csv)}", x=0.06, ha="left", color=TEXT, fontsize=11)
    fig.tight_layout()
    output = args.output or os.path.splitext(args.csv)[0] + ".png"
    fig.savefig(output, facecolor=SURFACE)
    print(f"{output} に描きました")


if __name__ == "__main__":
    main()
