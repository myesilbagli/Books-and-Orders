import { useEffect, useMemo, useState } from 'react';
import MarketChart from './components/MarketChart';
import { groupFills, summarize } from './lib/fills';
import { loadChart, loadFills, loadIndex, loadReads, loadStructure } from './lib/data';
import { readMarkers } from './lib/reads';
import type { Bar, Fill, Horizon, Index, Structure, Timeframe, TurnRead } from './lib/types';

// A file as loaded, with what it was loaded for, e.g. '2026-02-04_AAPL/1m'.
interface Loaded<T> {
  key: string;
  data: T;
}

const INTERVAL: Record<Timeframe, number> = { '1s': 1, '15s': 15, '1m': 60, '5m': 300 };

export default function App() {
  const [index, setIndex] = useState<Index | null>(null);
  const [dayId, setDayId] = useState('');
  const [run, setRun] = useState('');
  const [timeframe, setTimeframe] = useState<Timeframe>('1m');
  const [horizon, setHorizon] = useState<Horizon>('10s');
  // Off at first: the swing size c is chosen by eye with the outcomes hidden (D48).
  const [showFills, setShowFills] = useState(false);
  const [showStructure, setShowStructure] = useState(true);
  const [c, setC] = useState('2');
  // Each load remembers what it was for (day, timeframe, c). The chart only gets data that
  // matches the current choice: one stale file (the last day's ZigZag) would stretch the time
  // axis over two days and push the new bars off screen.
  const [structure, setStructure] = useState<Loaded<Structure> | null>(null);
  const [showBands, setShowBands] = useState(false);
  // Trend vs range from structure + ER (D58); the founder moves the two lines by eye.
  const [showRegime, setShowRegime] = useState(true);
  const [erLow, setErLow] = useState(0.3);
  const [erHigh, setErHigh] = useState(0.6);
  const [bars, setBars] = useState<Loaded<Bar[]> | null>(null);
  const [fills, setFills] = useState<Loaded<Fill[]> | null>(null);
  const [showReads, setShowReads] = useState(true);
  const [reads, setReads] = useState<Loaded<TurnRead[]> | null>(null);
  const [fit, setFit] = useState(0); // bumped by the Full day button
  const [error, setError] = useState('');

  useEffect(() => {
    loadIndex()
      .then((i) => {
        setIndex(i);
        const first = i.days[0];
        if (first) {
          setDayId(first.id);
          setRun(first.runs.includes('naive_0us') ? 'naive_0us' : first.runs[0]);
        }
      })
      .catch((e: Error) => setError(e.message));
  }, []);

  useEffect(() => {
    if (!dayId) return;
    let current = true; // ignore a slow answer that arrives after the user switched again
    loadChart(dayId, timeframe)
      .then((b) => current && setBars({ key: `${dayId}/${timeframe}`, data: b }))
      .catch((e: Error) => setError(e.message));
    return () => {
      current = false;
    };
  }, [dayId, timeframe]);

  useEffect(() => {
    if (!dayId || !run) return;
    let current = true;
    loadFills(dayId, run)
      .then((f) => current && setFills({ key: `${dayId}/${run}`, data: f }))
      .catch((e: Error) => setError(e.message));
    return () => {
      current = false;
    };
  }, [dayId, run]);

  useEffect(() => {
    if (!dayId) return;
    let current = true;
    loadReads(dayId)
      .then((r) => current && setReads({ key: dayId, data: r }))
      .catch(() => current && setReads({ key: dayId, data: [] })); // a day exported before the reads
    return () => {
      current = false;
    };
  }, [dayId]);

  useEffect(() => {
    if (!dayId || !showStructure) {
      setStructure(null);
      return;
    }
    let current = true;
    loadStructure(dayId, timeframe, c)
      .then((s) => current && setStructure({ key: `${dayId}/${timeframe}/${c}`, data: s }))
      .catch((e: Error) => setError(e.message));
    return () => {
      current = false;
    };
  }, [dayId, timeframe, c, showStructure]);

  // Until the new bars arrive the old ones stay up, but with nothing else of either day on them.
  const barsReady = bars?.key === `${dayId}/${timeframe}`;
  const shownFills = useMemo(
    () => (barsReady && fills?.key === `${dayId}/${run}` ? fills.data : []),
    [barsReady, fills, dayId, run],
  );
  const shownStructure =
    showStructure && barsReady && structure?.key === `${dayId}/${timeframe}/${c}` ? structure.data : null;

  // Recomputed only when its inputs change, not on every mouse move over the chart.
  const groups = useMemo(() => groupFills(shownFills, INTERVAL[timeframe], horizon), [shownFills, timeframe, horizon]);
  const summary = useMemo(() => summarize(shownFills, horizon), [shownFills, horizon]);
  const readMarks = useMemo(
    () => (showReads && barsReady && reads?.key === dayId ? readMarkers(reads.data, INTERVAL[timeframe]) : []),
    [showReads, barsReady, reads, dayId, timeframe],
  );
  const day = index?.days.find((d) => d.id === dayId);

  return (
    <div className="app">
      <header>
        <h1>Books and Orders</h1>
        <label>
          Day
          <select value={dayId} onChange={(e) => setDayId(e.target.value)}>
            {index?.days.map((d) => (
              <option key={d.id} value={d.id}>
                {d.date} {d.symbol}
              </option>
            ))}
          </select>
        </label>
        <label>
          Run
          <select value={run} onChange={(e) => setRun(e.target.value)}>
            {day?.runs.map((r) => (
              <option key={r}>{r}</option>
            ))}
          </select>
        </label>
        <Toggle options={['1s', '15s', '1m', '5m']} value={timeframe} onChange={setTimeframe} />
        <label>
          Markout
          <Toggle options={['1s', '10s', '60s']} value={horizon} onChange={setHorizon} />
        </label>
        <label className="check">
          <input type="checkbox" checked={showFills} onChange={(e) => setShowFills(e.target.checked)} /> Fills
        </label>
        <label className="check">
          <input type="checkbox" checked={showReads} onChange={(e) => setShowReads(e.target.checked)} /> Reads
          B1/B2
        </label>
        <label className="check">
          <input type="checkbox" checked={showBands} onChange={(e) => setShowBands(e.target.checked)} /> Bollinger
        </label>
        <label className="check">
          <input type="checkbox" checked={showStructure} onChange={(e) => setShowStructure(e.target.checked)} />{' '}
          Structure
        </label>
        {showStructure && (day?.structure_c?.length ?? 0) > 0 && (
          <label>
            swing = c × ATR, c
            <Toggle options={day?.structure_c ?? []} value={c} onChange={setC} />
          </label>
        )}
        {showStructure && (
          <label className="check">
            <input type="checkbox" checked={showRegime} onChange={(e) => setShowRegime(e.target.checked)} /> ER regime
          </label>
        )}
        {showStructure && showRegime && (
          <>
            {/* Each slider stops at the other, so the range line never passes the trend line. */}
            <Slider label="range ≤" value={erLow} onChange={(v) => setErLow(Math.min(v, erHigh))} />
            <Slider label="trend ≥" value={erHigh} onChange={(v) => setErHigh(Math.max(v, erLow))} />
          </>
        )}
        <button className="fit" onClick={() => setFit((n) => n + 1)}>
          Full day
        </button>
        <span className="summary">
          {summary.fills.toLocaleString()} fills · {summary.shares.toLocaleString()} shares ·{' '}
          {summary.markout === null ? '–' : `${summary.markout.toFixed(2)} ¢/share`} at {horizon}
        </span>
      </header>
      {error && <p className="error">{error}</p>}
      <MarketChart
        bars={bars?.data ?? []}
        fit={fit}
        groups={showFills ? groups : []}
        reads={readMarks}
        timeframe={timeframe}
        horizon={horizon}
        showBands={showBands}
        structure={shownStructure}
        showRegime={showRegime}
        erLow={erLow}
        erHigh={erHigh}
      />
    </div>
  );
}

// A row of buttons, one pressed. Generic over the option type (<T extends string>) so the
// value it hands back keeps its exact type, e.g. '1s' | '1m' rather than any string.
function Toggle<T extends string>(props: { options: T[]; value: T; onChange: (v: T) => void }) {
  return (
    <span className="toggle">
      {props.options.map((o) => (
        <button key={o} className={o === props.value ? 'on' : ''} onClick={() => props.onChange(o)}>
          {o}
        </button>
      ))}
    </span>
  );
}

// An ER threshold, 0-1 in steps of 0.05, with its value beside it.
function Slider(props: { label: string; value: number; onChange: (v: number) => void }) {
  return (
    <label className="slider">
      ER {props.label}
      <input
        type="range"
        min={0}
        max={1}
        step={0.05}
        value={props.value}
        onChange={(e) => props.onChange(Number(e.target.value))}
      />
      <span>{props.value.toFixed(2)}</span>
    </label>
  );
}
