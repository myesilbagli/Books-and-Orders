"""Cross-check chart_dump's bars and indicators against Databento's (D42).

    research/.venv/bin/python research/check_chart.py data/sim/chart/AAPL_chart.csv \\
        data/databento/AAPL_20191230_XNAS_ohlcv-1m.dbn.zst data/databento/AAPL_20191230_XNAS_ohlcv-1s.dbn.zst

Databento builds its bars from the same Nasdaq feed (XNAS.ITCH) with its own code, from
trade prices. Three checks:
  1. volume per bar: tests our trade extraction (E, printable C, P);
  2. close: our mid vs their last trade, expected within about half a spread;
  3. indicators: recomputed here, independently of the C++, on our closes (must match to
     rounding) and on their closes (differs only by mid vs trade).
"""

import sys
import warnings

import databento as db
import pandas as pd

warnings.filterwarnings("ignore", category=FutureWarning)
warnings.filterwarnings("ignore", message=".*inplace.*")

RTH_START, RTH_END = "09:30:00", "16:00:00"


# --- reference indicators: written from the textbook definitions, not from the C++ ------

def sma(xs, n):
    return [None if i + 1 < n else sum(xs[i + 1 - n:i + 1]) / n for i in range(len(xs))]


def ema(xs, n):
    """alpha = 2/(n+1), first value = SMA of the first n (TradingView's ta.ema)."""
    out, e, alpha = [], None, 2 / (n + 1)
    for i, x in enumerate(xs):
        if e is None and i + 1 == n:
            e = sum(xs[:n]) / n
        elif e is not None:
            e = e + alpha * (x - e)
        out.append(e)
    return out


def rsi(xs, n):
    """Wilder: first averages are plain means of the first n changes, then alpha = 1/n."""
    out, g, l = [None], 0.0, 0.0
    for i in range(1, len(xs)):
        ch = xs[i] - xs[i - 1]
        gain, loss = max(ch, 0.0), max(-ch, 0.0)
        if i <= n:
            g, l = g + gain / n, l + loss / n
        else:
            g, l = (g * (n - 1) + gain) / n, (l * (n - 1) + loss) / n
        out.append(None if i < n else (50.0 if g + l == 0 else 100 * g / (g + l)))
    return out


def stoch_rsi(xs, n_rsi=14, n_stoch=14, k=3, d=3):
    r = rsi(xs, n_rsi)
    stoch = []
    for i, v in enumerate(r):
        window = [x for x in r[max(0, i + 1 - n_stoch):i + 1] if x is not None]
        if v is None or len(window) < n_stoch:
            continue  # not warm: nothing goes into %K yet
        lo, hi = min(window), max(window)
        stoch.append((i, 50.0 if hi == lo else 100 * (v - lo) / (hi - lo)))
    ks = sma([s for _, s in stoch], k)
    kd = [(stoch[j][0], kv) for j, kv in enumerate(ks) if kv is not None]
    ds = sma([kv for _, kv in kd], d)
    out_k, out_d = [None] * len(xs), [None] * len(xs)
    for j, (i, kv) in enumerate(kd):
        if ds[j] is not None:
            out_k[i], out_d[i] = kv, ds[j]
    return out_k, out_d


def add_indicators(df):
    c = df["close"].tolist()
    df["r_ema9"], df["r_ema21"], df["r_rsi"] = ema(c, 9), ema(c, 21), rsi(c, 14)
    df["r_stoch_k"], df["r_stoch_d"] = stoch_rsi(c)
    return df


# --- loading ------------------------------------------------------------------------------

def ours(path, timeframe):
    df = pd.read_csv(path)
    df = df[df["timeframe"] == timeframe].copy()
    df["time"] = df["time_et"]
    return df.set_index("time")


def theirs(path, freq, start_et):
    """Databento's bars, one per interval with a trade; quiet intervals filled flat with
    no volume, the way our bars are (D38)."""
    df = db.DBNStore.from_file(path).to_df()
    df.index = df.index.tz_convert("America/New_York")
    full = pd.date_range(f"2019-12-30 {start_et}", "2019-12-30 15:59:59",
                         freq=freq, tz="America/New_York")
    df = df.reindex(full)
    df["volume"] = df["volume"].fillna(0)
    df["close"] = df["close"].ffill()
    df = df.dropna(subset=["close"])
    df.index = df.index.strftime("%H:%M:%S")
    return df


def describe(label, s, unit):
    s = s.dropna().abs()
    print(f"  {label:<28} n={len(s):>6}  median {s.median():.4f}  95% {s.quantile(.95):.4f}"
          f"  max {s.max():.4f} {unit}")


def main(chart_csv, dbn_1m, dbn_1s):
    for tf, path, freq in (("1m", dbn_1m, "1min"), ("1s", dbn_1s, "1s")):
        o = ours(chart_csv, tf)
        t = theirs(path, freq, o.index[0])
        j = o.join(t[["close", "volume"]], rsuffix="_db", how="inner")
        rth = j[(j.index >= RTH_START) & (j.index < RTH_END)]
        print(f"\n== {tf} bars: {len(j)} common, {len(rth)} in the regular session")

        # 1. volume
        same = (rth["volume"] == rth["volume_db"]).mean()
        print(f"  volume identical in {100 * same:.1f}% of regular-session bars;"
              f" totals ours {rth['volume'].sum():,.0f} vs Databento {rth['volume_db'].sum():,.0f}")
        off = rth[rth["volume"] != rth["volume_db"]]
        for time, row in off.head(5).iterrows():
            print(f"    {time}  ours {row['volume']:>9,.0f}  Databento {row['volume_db']:>9,.0f}")

        # 2. close: our mid vs their last trade, in cents
        describe("close, mid vs trade", (rth["close"] - rth["close_db"]) * 100, "cents")

        # 3. indicators (1 min only: the chart a trader would look at)
        if tf != "1m":
            continue
        mine = add_indicators(o[["close"]].copy())
        db_ind = add_indicators(t.loc[t.index.isin(o.index), ["close"]].copy())
        print("  indicators: C++ vs this reference, both on OUR closes (code check):")
        for col, ref, unit, scale in (("ema_fast", "r_ema9", "cents", 100),
                                      ("ema_slow", "r_ema21", "cents", 100),
                                      ("rsi", "r_rsi", "points", 1),
                                      ("stoch_k", "r_stoch_k", "points", 1),
                                      ("stoch_d", "r_stoch_d", "points", 1)):
            d = (o[col] - pd.to_numeric(mine[ref], errors="coerce")) * scale
            describe(col, d, unit)
        print("  indicators: ours vs the same on Databento's trade closes (data check), regular session:")
        both = o.join(db_ind, how="inner", rsuffix="_db")
        both = both[(both.index >= RTH_START) & (both.index < RTH_END)]
        for col, ref, unit, scale in (("ema_fast", "r_ema9", "cents", 100),
                                      ("ema_slow", "r_ema21", "cents", 100),
                                      ("rsi", "r_rsi", "points", 1),
                                      ("stoch_k", "r_stoch_k", "points", 1)):
            d = (both[col] - pd.to_numeric(both[ref], errors="coerce")) * scale
            describe(col, d, unit)


if __name__ == "__main__":
    main(*sys.argv[1:4])
