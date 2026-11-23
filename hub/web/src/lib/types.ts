// The shapes research/hub_export.py writes (D46). Times are "chart seconds": Eastern
// wall-clock time written as if it were UTC, so the chart's axis reads ET as is.

export interface Bar {
  time: number;
  open: number; // dollars, from the mid
  high: number;
  low: number;
  close: number;
  volume: number; // shares traded in the bar
  // null while warming up (or before VWAP starts), never 0
  vwap: number | null;
  ema_fast: number | null;
  ema_slow: number | null;
  sma: number | null;
  bb_upper: number | null;
  bb_lower: number | null;
  bb_pct: number | null;
  rsi: number | null;
  stoch_k: number | null;
  stoch_d: number | null;
}

export type Horizon = '1s' | '10s' | '60s';

export interface Fill {
  time: number; // chart seconds, with the fraction
  side: 'B' | 'S'; // B: our bid was hit, we bought
  price: number; // dollars
  quantity: number;
  markout_1s: number; // cents a share: spread + adverse selection at that horizon
  markout_10s: number;
  markout_60s: number;
  touch_share: number;
  shares_left: number;
}

export interface Day {
  id: string; // e.g. 2019-12-30_AAPL, the folder name
  date: string;
  symbol: string;
  runs: string[];
}

export interface Index {
  days: Day[];
}
