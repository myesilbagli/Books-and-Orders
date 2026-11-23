import type { HistogramData, LineData, SeriesMarker, UTCTimestamp } from 'lightweight-charts';
import type { Swing } from './types';

// Drawing the market structure (D48-D50). The swings and scores come from the C++
// MarketStructure; this only spreads the score over the bars and picks colors.

const COLORS = {
  up: 'rgba(22, 163, 74, 0.12)',
  down: 'rgba(220, 38, 38, 0.12)',
  range: 'rgba(107, 114, 128, 0.10)',
  none: 'rgba(0, 0, 0, 0)', // before the first score: nothing drawn
};

// The score at each bar: the last change at or before it. Both lists are in time order,
// so one pass with a pointer is enough (no search per bar).
export function scoresAtBars(
  bars: { time: number }[],
  scores: { time: number; score: number }[],
): (number | null)[] {
  const out: (number | null)[] = [];
  let next = 0;
  let current: number | null = null;
  for (const b of bars) {
    while (next < scores.length && scores[next].time <= b.time) {
      current = scores[next].score;
      next += 1;
    }
    out.push(current);
  }
  return out;
}

export function structureColor(score: number | null): string {
  if (score === null) return COLORS.none;
  if (score === 2) return COLORS.up;
  if (score === -2) return COLORS.down;
  return COLORS.range; // 0 or +-1: mixed
}

// One full-height column per bar, drawn behind the candles: a histogram of 1s on its own
// hidden scale is the usual way to shade a chart's background bar by bar.
export function background(
  bars: { time: number }[],
  scores: { time: number; score: number }[],
): HistogramData<UTCTimestamp>[] {
  const at = scoresAtBars(bars, scores);
  return bars.map((b, i) => ({ time: b.time as UTCTimestamp, value: 1, color: structureColor(at[i]) }));
}

// The ZigZag line: each swing at the bar that made it. Times must strictly increase for
// the chart library; swings already do, but a repeat is dropped to be safe.
export function zigzag(swings: Swing[]): LineData<UTCTimestamp>[] {
  const out: LineData<UTCTimestamp>[] = [];
  for (const s of swings) {
    if (out.length > 0 && out[out.length - 1].time >= s.time) continue;
    out.push({ time: s.time as UTCTimestamp, value: s.price });
  }
  return out;
}

export function swingMarkers(swings: Swing[]): SeriesMarker<UTCTimestamp>[] {
  return swings.map((s) => ({
    time: s.time as UTCTimestamp,
    position: s.kind === 'H' ? 'aboveBar' : 'belowBar',
    shape: 'circle',
    size: 0.4,
    color: '#374151',
    text: s.label,
  }));
}
