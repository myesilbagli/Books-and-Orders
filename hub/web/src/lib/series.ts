import type { CandlestickData, LineData, UTCTimestamp, WhitespaceData } from 'lightweight-charts';
import type { Bar } from './types';

// Turns exported bars into the chart library's series. Nothing is computed here: every
// value comes from the C++ MarketState (D46).

type LineKey = 'vwap' | 'ema_fast' | 'ema_slow' | 'sma' | 'bb_upper' | 'bb_lower' | 'rsi' | 'stoch_k' | 'stoch_d' | 'er';

export function candles(bars: Bar[]): CandlestickData<UTCTimestamp>[] {
  return bars.map((b) => ({ time: b.time as UTCTimestamp, open: b.open, high: b.high, low: b.low, close: b.close }));
}

// A null value becomes whitespace ({time} alone): a gap in the line, not a 0.
export function line(bars: Bar[], key: LineKey): (LineData<UTCTimestamp> | WhitespaceData<UTCTimestamp>)[] {
  return bars.map((b) => {
    const value = b[key];
    return value === null ? { time: b.time as UTCTimestamp } : { time: b.time as UTCTimestamp, value };
  });
}

// The whole day the bars cover, first bar to last. The chart opens on it after every day or
// timeframe switch, anchored to the bars themselves, so nothing else can pull the view away.
export function dayRange(bars: { time: number }[]): { from: UTCTimestamp; to: UTCTimestamp } | null {
  if (bars.length === 0) return null;
  return { from: bars[0].time as UTCTimestamp, to: bars[bars.length - 1].time as UTCTimestamp };
}
