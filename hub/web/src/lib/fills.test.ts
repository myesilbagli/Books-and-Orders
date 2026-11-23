import { describe, expect, it } from 'vitest';
import { fillsInBar, groupFills, markerColor, summarize, toMarkers } from './fills';
import type { Fill } from './types';

const T = 1577698200; // 09:30:00 in chart seconds

function fill(time: number, side: 'B' | 'S', quantity: number, m10: number): Fill {
  return {
    time, side, price: 290, quantity,
    markout_1s: 0, markout_10s: m10, markout_60s: 0,
    touch_share: 0.5, shares_left: 100,
  };
}

describe('groupFills', () => {
  // Two buys in the same second become one group; the markout is share-weighted:
  // (100 x +1 + 300 x -1) / 400 = -0.5 cents.
  it('merges fills of one side in one bar, weighting by shares', () => {
    const groups = groupFills([fill(T + 0.2, 'B', 100, 1), fill(T + 0.9, 'B', 300, -1)], 1, '10s');
    expect(groups).toEqual([{ time: T, side: 'B', shares: 400, fills: 2, markout: -0.5 }]);
  });

  // A buy and a sell in the same bar stay apart: they are different trades.
  it('keeps the two sides apart, buys first', () => {
    const groups = groupFills([fill(T + 0.5, 'S', 100, 2), fill(T + 0.6, 'B', 100, -2)], 1, '10s');
    expect(groups.map((g) => g.side)).toEqual(['B', 'S']);
  });

  // On the 1 minute chart, 09:30:10 and 09:30:50 are the same bar.
  it('uses the chart interval for the bar', () => {
    const groups = groupFills([fill(T + 10, 'B', 100, 1), fill(T + 50, 'B', 100, 3)], 60, '10s');
    expect(groups).toEqual([{ time: T, side: 'B', shares: 200, fills: 2, markout: 2 }]);
  });

  // Output in time order, whatever the input order: the chart library requires it.
  it('sorts by time', () => {
    const groups = groupFills([fill(T + 5, 'B', 100, 1), fill(T + 1, 'B', 100, 1)], 1, '10s');
    expect(groups.map((g) => g.time)).toEqual([T + 1, T + 5]);
  });
});

describe('markerColor', () => {
  it('is green when we made money, red when we lost, grey at zero', () => {
    expect(markerColor(0.4)).toBe('#16a34a');
    expect(markerColor(-0.4)).toBe('#dc2626');
    expect(markerColor(0)).toBe('#9ca3af');
  });
});

describe('toMarkers', () => {
  // A buy is an up arrow under the bar, a sell a down arrow above it.
  it('places buys below and sells above', () => {
    const markers = toMarkers([
      { time: T, side: 'B', shares: 100, fills: 1, markout: 1 },
      { time: T, side: 'S', shares: 100, fills: 1, markout: -1 },
    ]);
    expect(markers[0]).toMatchObject({ time: T, position: 'belowBar', shape: 'arrowUp', color: '#16a34a' });
    expect(markers[1]).toMatchObject({ time: T, position: 'aboveBar', shape: 'arrowDown', color: '#dc2626' });
  });
});

describe('fillsInBar', () => {
  it('finds the groups of one bar, for the legend', () => {
    const groups = groupFills([fill(T + 0.5, 'B', 100, 1), fill(T + 3, 'S', 100, 1)], 1, '10s');
    expect(fillsInBar(groups, T)).toHaveLength(1);
    expect(fillsInBar(groups, T + 1)).toHaveLength(0);
  });
});

describe('summarize', () => {
  // Same weighting as the bootstrap's cents a share: (100 x +1 + 300 x -1) / 400.
  it('counts fills and shares and weights the markout by shares', () => {
    expect(summarize([fill(T, 'B', 100, 1), fill(T + 9, 'S', 300, -1)], '10s')).toEqual({
      fills: 2, shares: 400, markout: -0.5,
    });
  });

  it('has no markout without fills', () => {
    expect(summarize([], '10s')).toEqual({ fills: 0, shares: 0, markout: null });
  });
});
