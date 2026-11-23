import {
  CandlestickSeries,
  createChart,
  createSeriesMarkers,
  LineSeries,
  LineStyle,
  type IChartApi,
  type ISeriesApi,
  type ISeriesMarkersPluginApi,
  type MouseEventParams,
  type Time,
  type UTCTimestamp,
} from 'lightweight-charts';
import { useEffect, useMemo, useRef, useState } from 'react';
import { fillsInBar, toMarkers, type FillGroup } from '../lib/fills';
import { candles, line } from '../lib/series';
import type { Bar, Horizon } from '../lib/types';
import Legend from './Legend';

interface Props {
  bars: Bar[];
  groups: FillGroup[];
  timeframe: '1s' | '1m';
  horizon: Horizon;
  showBands: boolean;
}

// Everything the chart draws. Kept in a ref, not in state: the chart library owns these
// objects, and changing them must not re-render React.
interface Handles {
  chart: IChartApi;
  candles: ISeriesApi<'Candlestick'>;
  emaFast: ISeriesApi<'Line'>;
  emaSlow: ISeriesApi<'Line'>;
  vwap: ISeriesApi<'Line'>;
  bbUpper: ISeriesApi<'Line'>;
  bbLower: ISeriesApi<'Line'>;
  stochK: ISeriesApi<'Line'>;
  stochD: ISeriesApi<'Line'>;
  markers: ISeriesMarkersPluginApi<Time>; // the library types it by its general Time
}

const thin = { lineWidth: 1, priceLineVisible: false, lastValueVisible: false } as const;
const STOCH_PANE = 1; // below the price chart, sharing its time axis

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
    const candleSeries = chart.addSeries(CandlestickSeries, {
      upColor: '#16a34a', downColor: '#dc2626', borderVisible: false,
      wickUpColor: '#16a34a', wickDownColor: '#dc2626',
      priceFormat: { type: 'price', precision: 3, minMove: 0.0005 },
    });
    const h: Handles = {
      chart,
      candles: candleSeries,
      emaFast: chart.addSeries(LineSeries, { ...thin, color: '#f59e0b', lineWidth: 2 }),
      emaSlow: chart.addSeries(LineSeries, { ...thin, color: '#3b82f6', lineWidth: 2 }),
      vwap: chart.addSeries(LineSeries, { ...thin, color: '#a855f7', lineWidth: 2, lineStyle: LineStyle.Dashed }),
      bbUpper: chart.addSeries(LineSeries, { ...thin, color: '#9ca3af' }),
      bbLower: chart.addSeries(LineSeries, { ...thin, color: '#9ca3af' }),
      stochK: chart.addSeries(LineSeries, { ...thin, color: '#3b82f6' }, STOCH_PANE),
      stochD: chart.addSeries(LineSeries, { ...thin, color: '#f59e0b' }, STOCH_PANE),
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
    chart.panes()[STOCH_PANE]?.setHeight(140);

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
    h.chart.applyOptions({ timeScale: { secondsVisible: props.timeframe === '1s' } });
    if (props.timeframe === '1s') {
      // 25 K one-second bars don't fit on a screen: open on the first 10 minutes of quoting.
      const midnight = Math.floor(props.bars[0].time / 86_400) * 86_400;
      const from = midnight + 9 * 3600 + 31 * 60;
      h.chart.timeScale().setVisibleRange({ from: from as UTCTimestamp, to: (from + 600) as UTCTimestamp });
    } else {
      h.chart.timeScale().fitContent();
    }
  }, [props.bars, props.timeframe]);

  useEffect(() => {
    handles.current?.markers.setMarkers(toMarkers(props.groups));
  }, [props.groups]);

  useEffect(() => {
    const h = handles.current;
    h?.bbUpper.applyOptions({ visible: props.showBands });
    h?.bbLower.applyOptions({ visible: props.showBands });
  }, [props.showBands]);

  const bar = hovered === null ? null : (byTime.get(hovered) ?? null);
  const groups = hovered === null ? [] : fillsInBar(props.groups, hovered);
  return (
    <div className="chart-area">
      <Legend bar={bar} groups={groups} horizon={props.horizon} />
      <div ref={container} className="chart" />
    </div>
  );
}
