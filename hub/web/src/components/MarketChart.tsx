import {
  CandlestickSeries,
  createChart,
  HistogramSeries,
  createSeriesMarkers,
  LineSeries,
  LineStyle,
  type IChartApi,
  type IPriceLine,
  type ISeriesApi,
  type ISeriesMarkersPluginApi,
  type MouseEventParams,
  type Time,
  type UTCTimestamp,
} from 'lightweight-charts';
import { useEffect, useMemo, useRef, useState } from 'react';
import { fillsInBar, toMarkers, type FillGroup } from '../lib/fills';
import { candles, line } from '../lib/series';
import { regime, regimeBackground, type Regime } from '../lib/regime';
import { background, scoresAtBars, swingMarkers, zigzag } from '../lib/structure';
import type { Bar, Horizon, Structure, Timeframe } from '../lib/types';
import Legend from './Legend';

interface Props {
  bars: Bar[];
  groups: FillGroup[];
  timeframe: Timeframe;
  horizon: Horizon;
  showBands: boolean;
  structure: Structure | null; // null: not shown
  showRegime: boolean; // shade by structure + ER (D58) instead of structure alone
  erLow: number; // ER at or below: range
  erHigh: number; // ER at or above, in a +-2 structure: strong trend
}

// Everything the chart draws. Kept in a ref, not in state: the chart library owns these
// objects, and changing them must not re-render React.
interface Handles {
  chart: IChartApi;
  shade: ISeriesApi<'Histogram'>;
  zigzag: ISeriesApi<'Line'>;
  candles: ISeriesApi<'Candlestick'>;
  emaFast: ISeriesApi<'Line'>;
  emaSlow: ISeriesApi<'Line'>;
  vwap: ISeriesApi<'Line'>;
  bbUpper: ISeriesApi<'Line'>;
  bbLower: ISeriesApi<'Line'>;
  stochK: ISeriesApi<'Line'>;
  stochD: ISeriesApi<'Line'>;
  er: ISeriesApi<'Line'>;
  erLow: IPriceLine; // the two thresholds, moved by the sliders
  erHigh: IPriceLine;
  markers: ISeriesMarkersPluginApi<Time>; // the library types it by its general Time
}

const thin = { lineWidth: 1, priceLineVisible: false, lastValueVisible: false } as const;
const STOCH_PANE = 1; // below the price chart, sharing its time axis
const ER_PANE = 2;

export default function MarketChart(props: Props) {
  const container = useRef<HTMLDivElement>(null);
  const handles = useRef<Handles | null>(null);
  const [hovered, setHovered] = useState<number | null>(null);

  // Bars by time, for the legend: a lookup per mouse move instead of a search.
  const byTime = useMemo(() => new Map(props.bars.map((b) => [b.time, b])), [props.bars]);

  // Create the chart once; remove it when the page goes away (or React's dev mode
  // mounts twice).
  useEffect(() => {
    const chart = createChart(container.current!, {
      autoSize: true,
      layout: { background: { color: '#ffffff' }, textColor: '#374151', panes: { separatorColor: '#e5e7eb' } },
      grid: { vertLines: { color: '#f3f4f6' }, horzLines: { color: '#f3f4f6' } },
      timeScale: { timeVisible: true, secondsVisible: true },
    });
    // Added first, so it is drawn behind everything: the structure shading, on its own
    // hidden 0-1 scale that fills the pane's full height.
    const shade = chart.addSeries(HistogramSeries, {
      priceScaleId: 'shade', priceLineVisible: false, lastValueVisible: false,
    });
    chart.priceScale('shade').applyOptions({ visible: false, scaleMargins: { top: 0, bottom: 0 } });
    const candleSeries = chart.addSeries(CandlestickSeries, {
      upColor: '#16a34a', downColor: '#dc2626', borderVisible: false,
      wickUpColor: '#16a34a', wickDownColor: '#dc2626',
      priceFormat: { type: 'price', precision: 3, minMove: 0.0005 },
    });
    // Panes are made in order: a series for pane 2 before pane 1 exists would land in pane 1.
    const stochK = chart.addSeries(LineSeries, { ...thin, color: '#3b82f6' }, STOCH_PANE);
    const stochD = chart.addSeries(LineSeries, { ...thin, color: '#f59e0b' }, STOCH_PANE);
    const erSeries = chart.addSeries(LineSeries, { ...thin, color: '#0f766e', lineWidth: 2 }, ER_PANE);
    const threshold = { color: '#9ca3af', lineWidth: 1, lineStyle: LineStyle.Dashed, axisLabelVisible: true } as const;
    const h: Handles = {
      chart,
      shade,
      zigzag: chart.addSeries(LineSeries, { ...thin, color: '#111827', lineWidth: 2 }),
      candles: candleSeries,
      emaFast: chart.addSeries(LineSeries, { ...thin, color: '#f59e0b', lineWidth: 2 }),
      emaSlow: chart.addSeries(LineSeries, { ...thin, color: '#3b82f6', lineWidth: 2 }),
      vwap: chart.addSeries(LineSeries, { ...thin, color: '#a855f7', lineWidth: 2, lineStyle: LineStyle.Dashed }),
      bbUpper: chart.addSeries(LineSeries, { ...thin, color: '#9ca3af' }),
      bbLower: chart.addSeries(LineSeries, { ...thin, color: '#9ca3af' }),
      stochK,
      stochD,
      er: erSeries,
      erLow: erSeries.createPriceLine({ ...threshold, price: 0.3 }),
      erHigh: erSeries.createPriceLine({ ...threshold, price: 0.6 }),
      markers: createSeriesMarkers(candleSeries, []),
    };
    // Stoch RSI lives on 0-100 with the usual 20 / 80 lines; a fixed scale keeps those
    // lines in the same place as you scroll.
    for (const s of [h.stochK, h.stochD]) {
      s.applyOptions({ autoscaleInfoProvider: () => ({ priceRange: { minValue: 0, maxValue: 100 } }) });
    }
    // Small margins, or the scale reads up to 120 for a value that can't pass 100.
    h.stochK.priceScale().applyOptions({ scaleMargins: { top: 0.05, bottom: 0.05 } });
    for (const level of [20, 80]) {
      h.stochK.createPriceLine({ price: level, color: '#d1d5db', lineWidth: 1, lineStyle: LineStyle.Dotted, axisLabelVisible: true });
    }
    // ER is 0-1 by construction: a fixed scale, like the Stoch RSI's.
    h.er.applyOptions({ autoscaleInfoProvider: () => ({ priceRange: { minValue: 0, maxValue: 1 } }) });
    h.er.priceScale().applyOptions({ scaleMargins: { top: 0.05, bottom: 0.05 } });
    // Shares of the height, not pixels: fixed heights set one by one squeeze each other
    // (the last one set takes its room from the pane above it).
    const shares = [0.64, 0.2, 0.16]; // price, Stoch RSI, ER
    chart.panes().forEach((pane, i) => pane.setStretchFactor(shares[i] ?? 0.1));

    const onMove = (p: MouseEventParams) => setHovered(typeof p.time === 'number' ? p.time : null);
    chart.subscribeCrosshairMove(onMove);
    handles.current = h;
    return () => {
      chart.unsubscribeCrosshairMove(onMove);
      chart.remove();
      handles.current = null;
    };
  }, []);

  // New bars (another day or timeframe): replace every series and pick a sensible window.
  useEffect(() => {
    const h = handles.current;
    if (!h || props.bars.length === 0) return;
    h.candles.setData(candles(props.bars));
    h.emaFast.setData(line(props.bars, 'ema_fast'));
    h.emaSlow.setData(line(props.bars, 'ema_slow'));
    h.vwap.setData(line(props.bars, 'vwap'));
    h.bbUpper.setData(line(props.bars, 'bb_upper'));
    h.bbLower.setData(line(props.bars, 'bb_lower'));
    h.stochK.setData(line(props.bars, 'stoch_k'));
    h.stochD.setData(line(props.bars, 'stoch_d'));
    h.er.setData(line(props.bars, 'er'));
    const seconds = props.timeframe === '1s' || props.timeframe === '15s';
    h.chart.applyOptions({ timeScale: { secondsVisible: seconds } });
    if (seconds) {
      // Too many bars for one screen: open on the start of quoting, 10 minutes of 1 s bars
      // or an hour of 15 s bars.
      const midnight = Math.floor(props.bars[0].time / 86_400) * 86_400;
      const from = midnight + 9 * 3600 + 31 * 60;
      const span = props.timeframe === '1s' ? 600 : 3600;
      h.chart.timeScale().setVisibleRange({ from: from as UTCTimestamp, to: (from + span) as UTCTimestamp });
    } else {
      h.chart.timeScale().fitContent();
    }
  }, [props.bars, props.timeframe]);

  // Structure: shading and ZigZag line, redrawn when the bars or the structure change.
  useEffect(() => {
    const h = handles.current;
    if (!h) return;
    const s = props.structure;
    h.shade.setData(
      !s ? [] : props.showRegime ? regimeBackground(props.bars, s.scores, props.erLow, props.erHigh) : background(props.bars, s.scores),
    );
    h.zigzag.setData(s ? zigzag(s.swings) : []);
  }, [props.bars, props.structure, props.showRegime, props.erLow, props.erHigh]);

  // The threshold lines follow the sliders.
  useEffect(() => {
    handles.current?.erLow.applyOptions({ price: props.erLow });
    handles.current?.erHigh.applyOptions({ price: props.erHigh });
  }, [props.erLow, props.erHigh]);

  // One marker list for the candles: fills and swing labels, in time order.
  useEffect(() => {
    const swings = props.structure ? swingMarkers(props.structure.swings) : [];
    const all = [...toMarkers(props.groups), ...swings].sort((a, b) => a.time - b.time);
    handles.current?.markers.setMarkers(all);
  }, [props.groups, props.structure]);

  // The score at each bar, for the legend; undefined while structure is off.
  const scoreByTime = useMemo(() => {
    if (!props.structure) return null;
    const at = scoresAtBars(props.bars, props.structure.scores);
    return new Map(props.bars.map((b, i) => [b.time, at[i]]));
  }, [props.bars, props.structure]);

  useEffect(() => {
    const h = handles.current;
    h?.bbUpper.applyOptions({ visible: props.showBands });
    h?.bbLower.applyOptions({ visible: props.showBands });
  }, [props.showBands]);

  const bar = hovered === null ? null : (byTime.get(hovered) ?? null);
  const groups = hovered === null ? [] : fillsInBar(props.groups, hovered);
  const score = scoreByTime && hovered !== null ? (scoreByTime.get(hovered) ?? null) : undefined;
  const state: Regime | undefined =
    props.showRegime && score !== undefined && bar ? regime(score, bar.er, props.erLow, props.erHigh) : undefined;
  return (
    <div className="chart-area">
      <Legend
        bar={bar}
        groups={groups}
        horizon={props.horizon}
        score={score}
        regime={state}
      />
      <div ref={container} className="chart" />
    </div>
  );
}
