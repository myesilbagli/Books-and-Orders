import { describe, expect, it } from 'vitest';
import { REGIME_COLORS, regime, regimeBackground } from './regime';

// Thresholds as the hub opens: ER <= 0.3 choppy, ER >= 0.6 efficient (D58).
const LOW = 0.3;
const HIGH = 0.6;

describe('regime', () => {
  // A clean structure trend (+-2) with an efficient path is a strong trend.
  it('is a strong trend when the structure is +-2 and ER is at or above the upper line', () => {
    expect(regime(2, 0.6, LOW, HIGH)).toBe('strong-up');
    expect(regime(-2, 0.9, LOW, HIGH)).toBe('strong-down');
  });

  // Higher highs and higher lows, but the path between them zigzags: a weak trend.
  it('is a weak trend when the structure is +-2 and ER is between the lines', () => {
    expect(regime(2, 0.45, LOW, HIGH)).toBe('weak-up');
    expect(regime(-2, 0.31, LOW, HIGH)).toBe('weak-down');
  });

  // Choppy path: a range, whatever the structure says (ER wins at the low end).
  it('is a range when ER is at or below the lower line, even in a structure trend', () => {
    expect(regime(0, 0.3, LOW, HIGH)).toBe('range');
    expect(regime(2, 0.1, LOW, HIGH)).toBe('range');
    expect(regime(-1, 0.0, LOW, HIGH)).toBe('range');
  });

  // An efficient move without a clean structure (score 0 or +-1): we don't call it.
  it('is unclear when ER is above the lower line but the structure is not +-2', () => {
    expect(regime(1, 0.8, LOW, HIGH)).toBe('unclear');
    expect(regime(0, 0.5, LOW, HIGH)).toBe('unclear');
  });

  // Either reading still warming up: nothing to say.
  it('is none while the score or ER is missing', () => {
    expect(regime(null, 0.8, LOW, HIGH)).toBe('none');
    expect(regime(2, null, LOW, HIGH)).toBe('none');
  });
});

describe('regimeBackground', () => {
  // The score carried forward from its change points, ER from the bar itself.
  it('colors each bar from the score known then and the bar\'s own ER', () => {
    const bars = [
      { time: 10, er: 0.9 },
      { time: 20, er: 0.9 },
      { time: 30, er: 0.2 },
      { time: 40, er: null },
    ];
    const cols = regimeBackground(bars, [{ time: 20, score: 2 }], LOW, HIGH);
    expect(cols.map((c) => c.color)).toEqual([
      REGIME_COLORS.none, // no score yet
      REGIME_COLORS['strong-up'],
      REGIME_COLORS.range,
      REGIME_COLORS.none, // ER missing
    ]);
    expect(cols.every((c) => c.value === 1)).toBe(true); // full height, like the structure shading
  });
});
