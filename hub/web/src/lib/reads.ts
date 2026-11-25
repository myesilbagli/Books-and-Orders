import type { SeriesMarker, UTCTimestamp } from 'lightweight-charts';
import type { TurnRead } from './types';

// The turn reads (D63) as chart markers, for checking them by eye with the fills hidden.
const COLORS = { B1: '#7c3aed', B2: '#0891b2' } as const;

// Each read goes on the bar that holds the last second of its completing 15 s bar: that
// bar itself on 15 s, its minute on 1 min, the second it became known on 1 s. A longer bar
// can hold several reads; equal ones (same read, same way) show once.
export function readMarkers(reads: TurnRead[], barSeconds: number): SeriesMarker<UTCTimestamp>[] {
  const seen = new Set<string>();
  const out: SeriesMarker<UTCTimestamp>[] = [];
  for (const r of reads) {
    const time = Math.floor((r.known - 1) / barSeconds) * barSeconds;
    const key = `${time}/${r.read}/${r.dir}`;
    if (seen.has(key)) continue;
    seen.add(key);
    out.push({
      time: time as UTCTimestamp,
      position: r.dir < 0 ? 'aboveBar' : 'belowBar',
      shape: r.dir < 0 ? 'arrowDown' : 'arrowUp',
      color: COLORS[r.read],
      text: r.read,
    });
  }
  return out;
}
