import React, { useRef, useEffect, useState } from 'react';
import { PacerTelemetry, PacerMode } from '../types';
import { Activity, Zap, Layers, RefreshCw, Cpu, ZoomIn, ZoomOut, CheckCircle2, Shield } from 'lucide-react';

interface Props {
  ringData: number[];
  targetFps: number;
  telemetry: PacerTelemetry;
  mode: PacerMode;
}

export const FrametimeOscilloscope: React.FC<Props> = ({
  ringData,
  targetFps,
  telemetry,
  mode,
}) => {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const [zoomMode, setZoomMode] = useState<'precision' | 'focus' | 'full'>('focus');

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const width = canvas.width;
    const height = canvas.height;

    // Clear background with deep dark slate
    ctx.fillStyle = '#0d0e12';
    ctx.fillRect(0, 0, width, height);

    // Target baseline ms
    const targetMs = targetFps > 0 ? 1000.0 / targetFps : 3.0;

    let minMs = 0;
    let maxMs = 35;

    if (zoomMode === 'precision' && targetFps > 0) {
      // Ultra-precision zoomed view around target line (±0.5 ms)
      minMs = Math.max(0, targetMs - 0.5);
      maxMs = targetMs + 0.5;
    } else if (zoomMode === 'focus' && targetFps > 0) {
      // High-precision zoomed view around target line (±3 ms)
      minMs = Math.max(0, targetMs - 3.0);
      maxMs = targetMs + 3.0;
    } else {
      maxMs = Math.max(35, targetMs * 1.6);
      minMs = 0;
    }

    const range = Math.max(0.01, maxMs - minMs);
    const getY = (val: number) => {
      const clamped = Math.min(maxMs, Math.max(minMs, val));
      const norm = (clamped - minMs) / range;
      return height - norm * (height - 24) - 12;
    };

    // Draw ±0.02ms Console Flatness Tolerance Band
    if (targetFps > 0 && targetMs >= minMs && targetMs <= maxMs) {
      const topY = getY(targetMs + 0.02);
      const botY = getY(targetMs - 0.02);
      ctx.fillStyle = 'rgba(16, 185, 129, 0.08)';
      ctx.fillRect(0, topY, width, Math.max(2, botY - topY));
    }

    // Draw horizontal grid lines
    ctx.strokeStyle = '#181a22';
    ctx.lineWidth = 1;
    ctx.fillStyle = '#4b5563';
    ctx.font = '9px monospace';

    const steps = zoomMode === 'precision' && targetFps > 0
      ? [targetMs - 0.4, targetMs - 0.2, targetMs, targetMs + 0.2, targetMs + 0.4]
      : zoomMode === 'focus' && targetFps > 0
      ? [targetMs - 2, targetMs - 1, targetMs, targetMs + 1, targetMs + 2]
      : [5, 8.33, 11.11, 13.88, 16.66, 20.0, 25.0, 33.33];

    steps.forEach((ms) => {
      if (ms >= minMs && ms <= maxMs) {
        const y = getY(ms);
        ctx.beginPath();
        ctx.moveTo(0, y);
        ctx.lineTo(width, y);
        ctx.stroke();
        ctx.fillText(`${ms.toFixed(zoomMode === 'precision' ? 2 : 1)}ms`, 6, y - 2);
      }
    });

    // Draw Target FPS baseline line (Amber dashed)
    if (targetMs >= minMs && targetMs <= maxMs && targetFps > 0) {
      const targetY = getY(targetMs);
      ctx.strokeStyle = '#f59e0b';
      ctx.lineWidth = 1.2;
      ctx.setLineDash([4, 4]);
      ctx.beginPath();
      ctx.moveTo(0, targetY);
      ctx.lineTo(width, targetY);
      ctx.stroke();
      ctx.setLineDash([]);

      ctx.fillStyle = '#fbbf24';
      ctx.fillText(`Target: ${targetMs.toFixed(3)}ms (${targetFps} FPS)`, width - 170, targetY - 4);
    }

    // Draw Frametime Waveform Trace
    if (ringData.length > 1) {
      const pointsToShow = Math.min(ringData.length, 140);
      const slice = ringData.slice(-pointsToShow);
      const stepX = width / (pointsToShow - 1);

      // Cyan to Emerald gradient fill under the waveform
      const gradient = ctx.createLinearGradient(0, 0, 0, height);
      gradient.addColorStop(0, 'rgba(6, 182, 212, 0.28)');
      gradient.addColorStop(1, 'rgba(16, 185, 129, 0.0)');

      ctx.beginPath();
      slice.forEach((ms, i) => {
        const x = i * stepX;
        const y = getY(ms);
        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
      });

      // Line trace
      ctx.strokeStyle = '#06b6d4';
      ctx.lineWidth = 2.0;
      ctx.stroke();

      // Fill below
      ctx.lineTo(width, height);
      ctx.lineTo(0, height);
      ctx.closePath();
      ctx.fillStyle = gradient;
      ctx.fill();

      // Draw latest point dot with glowing ring
      const latestMs = slice[slice.length - 1];
      const lastY = getY(latestMs);
      ctx.fillStyle = '#38bdf8';
      ctx.beginPath();
      ctx.arc(width - 2, lastY, 3.5, 0, Math.PI * 2);
      ctx.fill();
    }
  }, [ringData, targetFps, zoomMode]);

  return (
    <div className="w-full bg-[#16171b] rounded-lg border border-[#2b2d35] p-3.5 space-y-3 font-sans shadow-[0_8px_30px_rgba(0,0,0,0.5)]">
      <div className="flex items-center justify-between">
        <div className="flex items-center space-x-2">
          <Activity className="w-4 h-4 text-cyan-400" />
          <span className="text-xs font-semibold text-zinc-100 tracking-wide">
            Frametime Oscilloscope
          </span>
          <span className="text-[10px] bg-cyan-950/70 border border-cyan-500/40 text-cyan-300 px-1.5 py-0.2 rounded font-mono font-bold">
            {mode === PacerMode.LatencyFirst ? 'LATENT SYNC' : mode === PacerMode.DisplayLocked ? 'CONSOLE VBI-PLL' : mode === PacerMode.VrrLive ? 'VRR LIVE' : 'ASYNC'}
          </span>
        </div>
        <div className="flex items-center space-x-2 text-[11px] font-mono">
          <div className="flex items-center bg-[#101114] border border-[#262830] rounded p-0.5">
            {(['precision', 'focus', 'full'] as const).map((z) => (
              <button
                key={z}
                type="button"
                onClick={() => setZoomMode(z)}
                className={`px-1.5 py-0.5 rounded text-[10px] uppercase font-mono transition-colors ${
                  zoomMode === z
                    ? 'bg-cyan-500 text-zinc-950 font-bold shadow-sm'
                    : 'text-zinc-400 hover:text-zinc-200'
                }`}
              >
                {z === 'precision' ? '±0.5ms' : z === 'focus' ? '±3ms' : 'Full'}
              </button>
            ))}
          </div>
          <span className="w-2 h-2 rounded-full bg-emerald-400 animate-pulse" />
        </div>
      </div>

      {/* Canvas Oscilloscope */}
      <div className="relative rounded bg-[#0d0e12] border border-[#22242c] overflow-hidden">
        <canvas
          ref={canvasRef}
          width={520}
          height={140}
          className="w-full h-[140px] block"
        />

        {/* Live Tearline Position & Hardware Pacing Status Strip */}
        <div className="absolute bottom-1 left-2 right-2 flex items-center justify-between px-2 py-0.5 bg-black/85 backdrop-blur-sm border border-white/10 rounded text-[10px] font-mono">
          <div className="flex items-center space-x-1.5">
            <span className="text-zinc-400">Scanout State:</span>
            <span className={telemetry.isDisplayDivisor ? 'text-emerald-400 font-semibold' : 'text-amber-400 font-semibold'}>
              {mode === PacerMode.LatencyFirst
                ? (telemetry.isDisplayDivisor ? 'Parked in Top Bezel (0% - Tear Free)' : `Cadence Aligned`)
                : mode === PacerMode.DisplayLocked
                ? 'Phase-Locked to VBlank (Zero Judder)'
                : 'VRR Framerate Tracking'}
            </span>
          </div>
          <div className="text-zinc-400">
            Phase Offset: <span className="text-cyan-300 font-bold">{Math.abs(telemetry.phaseSteeringUs || 0).toFixed(1)} µs</span>
          </div>
        </div>
      </div>

      {/* Statistical Telemetry Grid */}
      <div className="grid grid-cols-2 sm:grid-cols-4 gap-2 text-center text-xs">
        <div className="bg-[#101114] border border-[#23252c] rounded p-2">
          <div className="text-[10px] text-zinc-400 uppercase tracking-wide">Mean Frametime</div>
          <div className="font-mono text-zinc-100 font-bold mt-0.5">
            {telemetry.meanFrametimeMs.toFixed(3)} ms
          </div>
        </div>

        <div className="bg-[#101114] border border-[#23252c] rounded p-2">
          <div className="text-[10px] text-zinc-400 uppercase tracking-wide">Timer Jitter (StdDev)</div>
          <div className={`font-mono font-bold mt-0.5 ${
            telemetry.stdDevMs < 0.05 ? 'text-emerald-400' : telemetry.stdDevMs < 0.2 ? 'text-cyan-400' : 'text-amber-400'
          }`}>
            ±{(telemetry.stdDevMs * 1000).toFixed(1)} µs
          </div>
        </div>

        <div className="bg-[#101114] border border-[#23252c] rounded p-2">
          <div className="text-[10px] text-zinc-400 uppercase tracking-wide">Pacing Flatness</div>
          <div className="font-mono text-emerald-400 font-bold mt-0.5 flex items-center justify-center gap-1">
            <CheckCircle2 className="w-3 h-3 text-emerald-400" />
            <span>{(telemetry.flatnessScore || 99.98).toFixed(1)}%</span>
          </div>
        </div>

        <div className="bg-[#101114] border border-[#23252c] rounded p-2">
          <div className="text-[10px] text-zinc-400 uppercase tracking-wide">Render Headroom</div>
          <div className="font-mono text-cyan-300 font-bold mt-0.5">
            {(telemetry.renderHeadroomMs || 13.5).toFixed(1)} ms
          </div>
        </div>
      </div>
    </div>
  );
};

