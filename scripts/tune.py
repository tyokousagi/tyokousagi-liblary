#!/usr/bin/env python3
"""Optuna で pahcer を回し、パラメータを調整する。

C++ 側は template.cpp の param() でパラメータを読む（環境変数 AHC_<名前> を見る）。
  double T0 = param("T0", 1000.0);
このスクリプトは試行ごとに AHC_T0 などを設定して pahcer run を実行する。
コンテストのディレクトリ（pahcer_config.toml がある場所）で実行する。

- seed の順番をシャッフルして1ケースずつ結果を受け取り、ベストより明らかに悪い試行は
  WilcoxonPruner で途中で打ち切る（全ケース回さないので速い）
- エラー（WA など）が出た試行、--time-limit を超えた試行は失敗扱いにする
- 評価値は既定では「スコアの対数の平均」（= 幾何平均）。seed ごとのスコアの桁の違いに強い
- 結果は tune.db（SQLite）に保存されるので、同じコマンドで途中から再開できる
- --freeze-best-scores / --no-result-file 付きで実行するので、pahcer のベストや履歴は汚れない

使い方:
  python3 tune.py -p T0:float:10:10000:log -p T1:float:0.001:10:log --init T0=1000,T1=0.01
  python3 tune.py -p WIDTH:int:100:3000:log -n 50 --time-limit 2.0
  python3 tune.py -p MODE:cat:0,1,2

パラメータの書き方: 名前:型:下限:上限[:log]   型は int / float
                   名前:cat:値1,値2,...       （候補から選ぶ）
"""

import argparse
import json
import math
import os
import re
import signal
import subprocess
import sys
import time
import warnings

try:
    import optuna
except ImportError:
    sys.exit("optuna がありません: pip install optuna")

# WilcoxonPruner は experimental 扱いで毎回警告が出るので消す
warnings.filterwarnings("ignore", category=optuna.exceptions.ExperimentalWarning)


def parse_param(spec):
    parts = spec.split(":")
    if len(parts) < 3:
        sys.exit(f"パラメータの書き方が違います: {spec}（例: T0:float:10:10000:log）")
    name, kind = parts[0], parts[1]
    if kind == "cat":
        return {"name": name, "kind": kind, "choices": parts[2].split(",")}
    if kind not in ("int", "float") or len(parts) not in (4, 5) or (len(parts) == 5 and parts[4] != "log"):
        sys.exit(f"パラメータの書き方が違います: {spec}（例: T0:float:10:10000:log）")
    conv = int if kind == "int" else float
    return {"name": name, "kind": kind, "low": conv(parts[2]), "high": conv(parts[3]), "log": len(parts) == 5}


def suggest(trial, p):
    if p["kind"] == "int":
        return trial.suggest_int(p["name"], p["low"], p["high"], log=p["log"])
    if p["kind"] == "float":
        return trial.suggest_float(p["name"], p["low"], p["high"], log=p["log"])
    return trial.suggest_categorical(p["name"], p["choices"])


def read_objective(config_path):
    if os.path.exists(config_path):
        with open(config_path, encoding="utf-8") as f:
            m = re.search(r'^\s*objective\s*=\s*"(\w+)"', f.read(), re.M)
        if m:
            return m.group(1).lower() == "max"
    return True


def kill_group(proc):
    try:
        os.killpg(proc.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    proc.wait()


def main():
    parser = argparse.ArgumentParser(
        description="Optuna で pahcer を回してパラメータを調整する", formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("-p", "--param", action="append", required=True, help="名前:型:下限:上限[:log] または 名前:cat:値1,値2")
    parser.add_argument("-n", "--trials", type=int, default=100, help="試行回数（既定: 100）")
    parser.add_argument("--timeout", type=float, help="全体の制限時間[秒]")
    parser.add_argument("--init", help="最初に試す値。今の既定値を入れておくと打ち切りの基準になる。例: T0=1000,T1=0.01")
    parser.add_argument("--metric", choices=["log", "raw"], default="log", help="log: スコアの対数の平均（既定） / raw: スコアの平均")
    parser.add_argument("--time-limit", type=float, help="実行時間[秒]がこれを超えたケースがあれば、その試行は失敗扱い")
    parser.add_argument("--no-prune", action="store_true", help="途中での打ち切りをしない")
    parser.add_argument("--study", default="tune", help="study の名前（既定: tune）。別の調整を始めるときは変える")
    parser.add_argument("--storage", default="sqlite:///tune.db", help="結果の保存先（既定: sqlite:///tune.db）")
    parser.add_argument("--setting-file", default="pahcer_config.toml", help="pahcer の設定ファイル")
    args = parser.parse_args()

    params = [parse_param(s) for s in args.param]
    maximize = read_objective(args.setting_file)
    compiled = False

    def objective(trial):
        nonlocal compiled
        values = {p["name"]: suggest(trial, p) for p in params}
        env = os.environ.copy()
        for name, value in values.items():
            env[f"AHC_{name}"] = str(value)

        cmd = ["pahcer", "run", "--json", "--shuffle", "--freeze-best-scores", "--no-result-file"]
        cmd += ["--setting-file", args.setting_file]
        if compiled:
            cmd.append("--no-compile")
        # pahcer が起動した子プロセスごとまとめて止められるよう、別のプロセスグループで起動する
        proc = subprocess.Popen(cmd, env=env, stdout=subprocess.PIPE, text=True, start_new_session=True)
        metrics = {}  # seed -> 評価値
        try:
            for line in proc.stdout:
                line = line.strip()
                if not line.startswith("{"):
                    continue
                case = json.loads(line)
                compiled = True  # 結果が返ってきた = コンパイルは済んだ
                seed, score = case["seed"], case["score"]
                # エラーや時間超過は「悪い試行」として TrialPruned で終える（TPE は完了した試行より悪いとみなす）
                if case["error_message"] or score <= 0:
                    trial.set_user_attr("error", f"seed {seed:04}: {case['error_message'] or 'score 0'}")
                    raise optuna.TrialPruned()
                if args.time_limit and case["execution_time"] > args.time_limit:
                    trial.set_user_attr("error", f"seed {seed:04}: {case['execution_time']:.3f}s")
                    raise optuna.TrialPruned()
                metrics[seed] = math.log(score) if args.metric == "log" else float(score)
                trial.report(metrics[seed], seed)
                if not args.no_prune and trial.should_prune():
                    trial.set_user_attr("pruned_at", len(metrics))
                    return estimate_from_best(trial.study, metrics)
        finally:
            if proc.poll() is None:
                kill_group(proc)
        if proc.returncode != 0:
            if not compiled:
                sys.exit(f"pahcer run が失敗しました（終了コード {proc.returncode}）。コンパイルエラーなどを確認してください")
            raise RuntimeError(f"pahcer run が失敗しました（終了コード {proc.returncode}）")
        if not metrics:
            raise RuntimeError("pahcer の結果を読めませんでした")
        return sum(metrics.values()) / len(metrics)

    def estimate_from_best(study, metrics):
        """途中で打ち切った試行の評価値を、ベストとの同じ seed どうしの差から推定する。

        TrialPruned で終えると TPE がその情報をほぼ使えないので、推定値を返して完了扱いにする
        （Optuna の WilcoxonPruner のドキュメントで推奨されているやり方）。
        途中までの平均をそのまま返すと、簡単な seed ばかり回った試行がベストに化けることがあるので、
        ベストとの差の平均を使い、さらにベストを超えないよう抑える。
        """
        best = study.best_trial
        diffs = [v - best.intermediate_values[s] for s, v in metrics.items() if s in best.intermediate_values]
        estimate = best.value + (sum(diffs) / len(diffs) if diffs else 0.0)
        if maximize:
            return min(estimate, math.nextafter(best.value, -math.inf))
        return max(estimate, math.nextafter(best.value, math.inf))

    optuna.logging.set_verbosity(optuna.logging.WARNING)
    pruner = optuna.pruners.NopPruner() if args.no_prune else optuna.pruners.WilcoxonPruner(p_threshold=0.1)
    study = optuna.create_study(
        study_name=args.study,
        storage=args.storage,
        load_if_exists=True,
        direction="maximize" if maximize else "minimize",
        sampler=optuna.samplers.TPESampler(),
        pruner=pruner,
    )
    if args.init:
        init = {}
        for kv in args.init.split(","):
            name, value = kv.split("=")
            p = next((p for p in params if p["name"] == name), None)
            if p is None:
                sys.exit(f"--init の {name} は -p で指定されていません")
            init[name] = int(value) if p["kind"] == "int" else float(value) if p["kind"] == "float" else value
        study.enqueue_trial(init, skip_if_exists=True)

    def show(value):
        if value is None:
            return "-"
        return f"{math.exp(value):,.2f}（幾何平均）" if args.metric == "log" else f"{value:,.2f}"

    def callback(study, trial):
        if "error" in trial.user_attrs:
            state, value = "エラー", trial.user_attrs["error"]
        elif "pruned_at" in trial.user_attrs:
            state, value = "打切", show(trial.value) + "*"
        else:
            state = {"COMPLETE": "完了", "FAIL": "失敗"}.get(trial.state.name, trial.state.name)
            value = show(trial.value)
        values = " ".join(f"{k}={v:.6g}" if isinstance(v, float) else f"{k}={v}" for k, v in trial.params.items())
        sec = (trial.datetime_complete - trial.datetime_start).total_seconds()
        steps = len(trial.intermediate_values)
        print(f"#{trial.number:4d} {state} {sec:6.1f}s {steps:4d}ケース  {value:>24}  {values}", flush=True)

    print(f"目的: {'最大化' if maximize else '最小化'}   保存先: {args.storage}  study: {args.study}")
    print("（打切の値 * はベストとの差から推定したもの）")
    start = time.time()
    try:
        study.optimize(objective, n_trials=args.trials, timeout=args.timeout, callbacks=[callback], catch=(RuntimeError,))
    except KeyboardInterrupt:
        print("\n中断しました")
    print(f"\n経過時間: {time.time() - start:.0f}s")

    completed = [t for t in study.trials if t.state == optuna.trial.TrialState.COMPLETE]
    if not completed:
        print("完了した試行がありません")
        return
    best = study.best_trial
    print(f"ベスト: #{best.number}  {show(best.value)}")
    print("C++ の既定値を書き換えるとき:")
    for name, value in best.params.items():
        literal = f"{value:.6g}" if isinstance(value, float) else str(value)
        if isinstance(value, float) and re.fullmatch(r"-?\d+", literal):
            literal += ".0"
        print(f'  param("{name}", {literal})')


if __name__ == "__main__":
    main()
