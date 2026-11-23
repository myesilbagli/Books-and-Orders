import { describe, expect, it } from 'vitest';
import { background, scoresAtBars, structureColor, swingMarkers, zigzag } from './structure';
import type { Swing } from './types';

const bars = [10, 20, 30, 40, 50].map((time) => ({ time }));

describe('scoresAtBars', () => {
  // Each change holds until the next: no score before the first change.
  it('carries every score forward to the next change', () => {
    const scores = scoresAtBars(bars, [
      { time: 20, score: 2 },
      { time: 40, score: 0 },
    ]);
    expect(scores).toEqual([null, 2, 2, 0, 0]);
  });
});

describe('structureColor', () => {
  it('is green up, red down, grey for a range, nothing before a score', () => {
    expect(structureColor(2)).toBe('rgba(22, 163, 74, 0.12)');
    expect(structureColor(-2)).toBe('rgba(220, 38, 38, 0.12)');
    expect(structureColor(0)).toBe('rgba(107, 114, 128, 0.10)');
    expect(structureColor(1)).toBe('rgba(107, 114, 128, 0.10)');
    expect(structureColor(null)).toBe('rgba(0, 0, 0, 0)');
  });
});

describe('background', () => {
  // One full-height column per bar, colored by the structure known at that bar's close.
  it('colors every bar', () => {
    const cols = background(bars.slice(0, 2), [{ time: 20, score: -2 }]);
    expect(cols).toEqual([
      { time: 10, value: 1, color: 'rgba(0, 0, 0, 0)' },
      { time: 20, value: 1, color: 'rgba(220, 38, 38, 0.12)' },
    ]);
  });
});

const swings: Swing[] = [
  { time: 10, confirmed: 20, price: 100, kind: 'L', label: 'L' },
  { time: 30, confirmed: 40, price: 110, kind: 'H', label: 'H' },
  { time: 50, confirmed: 60, price: 105, kind: 'L', label: 'HL' },
];

describe('zigzag', () => {
  // The line joins the swings where they were made (not where they were confirmed).
  it('joins the swings at their highs and lows', () => {
    expect(zigzag(swings)).toEqual([
      { time: 10, value: 100 },
      { time: 30, value: 110 },
      { time: 50, value: 105 },
    ]);
  });
});

describe('swingMarkers', () => {
  // A high's label sits above the bar, a low's below, like on a hand-marked chart.
  it('labels highs above and lows below', () => {
    const m = swingMarkers(swings);
    expect(m[1]).toMatchObject({ time: 30, position: 'aboveBar', text: 'H' });
    expect(m[2]).toMatchObject({ time: 50, position: 'belowBar', text: 'HL' });
  });
});
