import type { HistogramData, UTCTimestamp } from 'lightweight-charts';
import { scoresAtBars } from './structure';

// Trend vs range by eye (D58): the C++ structure score gives the direction, the C++
// efficiency ratio (D57) how clean the path is. The page only compares them with two
// thresholds the founder moves; it computes no indicator.

export type Regime = 'strong-up' | 'strong-down' | 'weak-up' | 'weak-down' | 'range' | 'unclear' | 'none';

export const REGIME_COLORS: Record<Regime, string> = {
  'strong-up': 'rgba(22, 163, 74, 0.30)',
  'strong-down': 'rgba(220, 38, 38, 0.30)',
  'weak-up': 'rgba(22, 163, 74, 0.10)',
  'weak-down': 'rgba(220, 38, 38, 0.10)',
  range: 'rgba(107, 114, 128, 0.18)',
  unclear: 'rgba(0, 0, 0, 0)', // an efficient move without a clean structure: not called
  none: 'rgba(0, 0, 0, 0)',
};

export const REGIME_NAMES: Record<Regime, string> = {
  'strong-up': 'strong trend up',
  'strong-down': 'strong trend down',
  'weak-up': 'weak trend up',
  'weak-down': 'weak trend down',
  range: 'range',
  unclear: 'unclear',
  none: '–',
};

export function regime(score: number | null, er: number | null, low: number, high: number): Regime {
  if (score === null || er === null) return 'none';
  // Checked first: a choppy path is a range even inside a higher-high / higher-low swing,
  // because ER sees the last 10 bars and the swings can be much older.
  if (er <= low) return 'range';
  if (score === 2) return er >= high ? 'strong-up' : 'weak-up';
  if (score === -2) return er >= high ? 'strong-down' : 'weak-down';
  return 'unclear';
}

// Same full-height columns as the structure shading, one per bar.
export function regimeBackground(
  bars: { time: number; er: number | null }[],
  scores: { time: number; score: number }[],
  low: number,
  high: number,
): HistogramData<UTCTimestamp>[] {
  const at = scoresAtBars(bars, scores);
  return bars.map((b, i) => ({
    time: b.time as UTCTimestamp,
    value: 1,
    color: REGIME_COLORS[regime(at[i], b.er, low, high)],
  }));
}
