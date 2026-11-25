import { describe, expect, it } from 'vitest';
import { readMarkers } from './reads';
import type { TurnRead } from './types';

const T = 1577701860; // 10:31:00 in chart seconds

function read(time: number, name: 'B1' | 'B2', dir: 1 | -1): TurnRead {
  return { time, known: time + 15, read: name, dir };
}

describe('readMarkers', () => {
  // On the 15 s chart a read sits on the bar that completed it: down above, up below.
  it('marks the completing bar', () => {
    const m = readMarkers([read(T + 15, 'B1', -1), read(T + 30, 'B2', 1)], 15);
    expect(m).toEqual([
      { time: T + 15, position: 'aboveBar', shape: 'arrowDown', color: '#7c3aed', text: 'B1' },
      { time: T + 30, position: 'belowBar', shape: 'arrowUp', color: '#0891b2', text: 'B2' },
    ]);
  });

  // On 1 min, the minute holding the completing bar; two equal reads in it show once.
  it('one marker per bar, read and direction on a longer chart', () => {
    const m = readMarkers([read(T, 'B1', -1), read(T + 30, 'B1', -1), read(T + 45, 'B1', 1)], 60);
    expect(m.map((x) => [x.time, x.text, x.shape])).toEqual([
      [T, 'B1', 'arrowDown'],
      [T, 'B1', 'arrowUp'],
    ]);
  });

  // On 1 s, the last second of the completing bar: the moment it became known.
  it('the last second on the 1 s chart', () => {
    expect(readMarkers([read(T, 'B2', 1)], 1)[0].time).toBe(T + 14);
  });
});
