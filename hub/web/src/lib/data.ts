import type { Bar, Fill, Index, Structure, Timeframe } from './types';

// Loads the exporter's JSON. Each file is fetched once: the 1 s chart is ~6 MB, and
// switching run or timeframe back and forth shouldn't download it again.
const cache = new Map<string, Promise<unknown>>();

function load<T>(path: string): Promise<T> {
  let p = cache.get(path);
  if (!p) {
    p = fetch(path).then((r) => {
      if (!r.ok) throw new Error(`${path}: ${r.status}. Run research/hub_export.py first.`);
      return r.json();
    });
    p.catch(() => cache.delete(path)); // a failed load may be retried after an export
    cache.set(path, p);
  }
  return p as Promise<T>;
}

export const loadIndex = () => load<Index>('/index.json');
export const loadChart = (day: string, timeframe: Timeframe) => load<Bar[]>(`/${day}/chart_${timeframe}.json`);
export const loadStructure = (day: string, timeframe: Timeframe, c: string) =>
  load<Structure>(`/${day}/structure_${timeframe}_c${c}.json`);
export const loadFills = (day: string, run: string) => load<Fill[]>(`/${day}/fills_${run}.json`);
