import { describe, expect, it } from 'vitest';
import { candles, dayRange, line } from './series';
import type { Bar } from './types';

function bar(time: number, close: number, ema: number | null): Bar {
  return {
    time, open: close, high: close, low: close, close, volume: 0,
    vwap: null, ema_fast: ema, ema_slow: null, sma: null, bb_upper: null, bb_lower: null,
    bb_pct: null, rsi: null, stoch_k: null, stoch_d: null, atr: null, structure: null, er: null,
  };
}

describe('line', () => {
  // A warming-up indicator is a gap ({time} alone), not a 0 that would drag the line
  // to the bottom of the chart.
  it('leaves a gap where the value is null', () => {
    expect(line([bar(1, 10, null), bar(2, 11, 10.5)], 'ema_fast')).toEqual([
      { time: 1 },
      { time: 2, value: 10.5 },
    ]);
  });
});

describe('candles', () => {
  it('keeps open, high, low and close', () => {
    expect(candles([bar(1, 10, null)])).toEqual([{ time: 1, open: 10, high: 10, low: 10, close: 10 }]);
  });
});

describe('dayRange', () => {
  // The whole day the bars cover, first bar to last: what the chart opens on after a switch.
  it('spans the first bar to the last', () => {
    expect(dayRange([bar(100, 10, null), bar(160, 11, null), bar(220, 12, null)])).toEqual({ from: 100, to: 220 });
  });

  it('is null with no bars yet', () => {
    expect(dayRange([])).toBeNull();
  });
});
