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
  atr: number | null; // dollars
  structure: number | null; // at c = 2; the hub uses the chosen c's file instead
  er: number | null; // efficiency ratio over 10 bars, 0-1 (D57)
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
  structure_c?: string[]; // swing sizes exported, e.g. ["1", "1.5", "2", "3"]
}

export type Timeframe = '1s' | '15s' | '1m' | '5m';

// Market structure for one timeframe and one swing size c (D48, D49).
export interface Swing {
  time: number; // the bar that made the high or low
  confirmed: number; // when it was known: the end of the bar that confirmed it
  price: number; // dollars
  kind: 'H' | 'L';
  label: string; // HH, HL, LH, LL; H or L for the first; EH, EL for an equal one
}

export interface Structure {
  swings: Swing[];
  scores: { time: number; score: number }[]; // only where the score changed
}

export interface Index {
  days: Day[];
}
