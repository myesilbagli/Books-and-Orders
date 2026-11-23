import type { SeriesMarker, UTCTimestamp } from 'lightweight-charts';
import type { Fill, Horizon } from './types';

// Our fills, grouped by the bar they fall in. A sweep can fill us several times in one
// second, and a 1 minute bar holds dozens of fills: one marker per side per bar keeps the
// chart readable, and the legend lists the details.
export interface FillGroup {
  time: number; // the bar's start, chart seconds
  side: 'B' | 'S';
  shares: number;
  fills: number;
  markout: number; // cents a share, share-weighted over the group's fills
}

const COLORS = { good: '#16a34a', bad: '#dc2626', flat: '#9ca3af' };

export function groupFills(fills: Fill[], intervalSec: number, horizon: Horizon): FillGroup[] {
  // Sums per (bar, side). The key is a string because a JS Map compares objects by
  // identity, not by value.
  const sums = new Map<string, { time: number; side: 'B' | 'S'; shares: number; fills: number; pnl: number }>();
  for (const f of fills) {
    const time = Math.floor(f.time / intervalSec) * intervalSec;
    const key = `${time}${f.side}`;
    const s = sums.get(key) ?? { time, side: f.side, shares: 0, fills: 0, pnl: 0 };
    s.shares += f.quantity;
    s.fills += 1;
    s.pnl += f[`markout_${horizon}`] * f.quantity; // cents x shares, so bigger fills count more
    sums.set(key, s);
  }
  const groups = [...sums.values()].map((s) => ({
    time: s.time,
    side: s.side,
    shares: s.shares,
    fills: s.fills,
    markout: s.pnl / s.shares,
  }));
  // Time order (the chart library requires it), buys before sells within a bar.
  return groups.sort((a, b) => a.time - b.time || a.side.localeCompare(b.side));
}

export function markerColor(markout: number): string {
  if (markout > 0) return COLORS.good;
  if (markout < 0) return COLORS.bad;
  return COLORS.flat;
}

export function toMarkers(groups: FillGroup[]): SeriesMarker<UTCTimestamp>[] {
  return groups.map((g) => ({
    time: g.time as UTCTimestamp,
    position: g.side === 'B' ? 'belowBar' : 'aboveBar',
    shape: g.side === 'B' ? 'arrowUp' : 'arrowDown',
    color: markerColor(g.markout),
  }));
}

export function fillsInBar(groups: FillGroup[], time: number): FillGroup[] {
  return groups.filter((g) => g.time === time);
}

// A whole run in one line: should match RESULTS.md's cents a share for the same run.
export function summarize(fills: Fill[], horizon: Horizon): { fills: number; shares: number; markout: number | null } {
  let shares = 0;
  let pnl = 0;
  for (const f of fills) {
    shares += f.quantity;
    pnl += f[`markout_${horizon}`] * f.quantity;
  }
  return { fills: fills.length, shares, markout: shares > 0 ? pnl / shares : null };
}
