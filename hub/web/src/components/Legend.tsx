import type { FillGroup } from '../lib/fills';
import { markerColor } from '../lib/fills';
import type { Bar, Horizon } from '../lib/types';

// The values of the bar under the mouse, and our fills in it. Everything as exported:
// nothing here is recomputed (D46).
function n(v: number | null, places = 2): string {
  return v === null ? '–' : v.toFixed(places);
}

function clock(time: number): string {
  return new Date(time * 1000).toISOString().slice(11, 19); // chart seconds are ET as UTC
}

export default function Legend(props: { bar: Bar | null; groups: FillGroup[]; horizon: Horizon }) {
  const b = props.bar;
  if (!b) return <div className="legend">Move over the chart to see a bar.</div>;
  return (
    <div className="legend">
      <span>{clock(b.time)} ET</span>
      <span>
        O {n(b.open, 3)} H {n(b.high, 3)} L {n(b.low, 3)} C {n(b.close, 3)} · vol {b.volume.toLocaleString()}
      </span>
      <span className="ema-fast">EMA9 {n(b.ema_fast, 3)}</span>
      <span className="ema-slow">EMA21 {n(b.ema_slow, 3)}</span>
      <span className="vwap">VWAP {n(b.vwap, 3)}</span>
      <span>%B {n(b.bb_pct, 3)}</span>
      <span>RSI {n(b.rsi)}</span>
      <span>
        Stoch {n(b.stoch_k)} / {n(b.stoch_d)}
      </span>
      {props.groups.map((g) => (
        <span key={g.side} style={{ color: markerColor(g.markout) }}>
          {g.side === 'B' ? 'bought' : 'sold'} {g.shares} ({g.fills} {g.fills === 1 ? 'fill' : 'fills'}) · {g.markout.toFixed(2)} ¢ at {props.horizon}
        </span>
      ))}
    </div>
  );
}
