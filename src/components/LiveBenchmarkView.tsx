import React, { useRef, useEffect, useState } from 'react';
import { PacerTelemetry, PacerMode } from '../types';
import { Play, Pause, RefreshCw, Flame, Gauge, ShieldCheck, Zap, Monitor, Eye, Activity } from 'lucide-react';

interface Props {
  targetFps: number;
  telemetry: PacerTelemetry;
  mode: PacerMode;
  onPaceFrame: (simulatedRenderTimeMs: number) => void;
  onInjectHitch?: (durationMs: number) => void;
}

export const LiveBenchmarkView: React.FC<Props> = ({
  targetFps,
  telemetry,
  mode,
  onPaceFrame,
  onInjectHitch,
}) => {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const [isRunning, setIsRunning] = useState<boolean>(true);
  const [workloadComplexity, setWorkloadComplexity] = useState<number>(2); // 1 = lightweight, 5 = heavy
  const [simulateSpike, setSimulateSpike] = useState<boolean>(false);
  const [motionSpeed, setMotionSpeed] = useState<'normal' | 'fast' | 'slow'>('normal');
  const frameCountRef = useRef<number>(0);
  const panPosRef = useRef<number>(0);
  const animFrameIdRef = useRef<number | null>(null);

  useEffect(() => {
    let lastTime = performance.now();

    const renderLoop = (time: number) => {
      if (!isRunning) return;

      const delta = time - lastTime;
      lastTime = time;

      // Simulated GPU/CPU render workload with optional spike
      let simulatedRenderTimeMs = 1.2 + workloadComplexity * 0.9;
      if (simulateSpike && Math.random() < 0.08) {
        simulatedRenderTimeMs += 14.0; // Simulate intermittent 14ms asset load / GC spike
      }

      // Call the PacerEngine simulated limiter
      onPaceFrame(simulatedRenderTimeMs);

      // Draw graphics on Canvas
      const canvas = canvasRef.current;
      if (canvas) {
        const ctx = canvas.getContext('2d');
        if (ctx) {
          const w = canvas.width;
          const h = canvas.height;
          const cx = w / 2 - 40;
          const cy = h / 2;

          frameCountRef.current++;
          const frame = frameCountRef.current;

          // Speed factor for motion panning bar
          const speedPx = motionSpeed === 'fast' ? 5.0 : motionSpeed === 'slow' ? 1.5 : 3.0;
          panPosRef.current = (panPosRef.current + speedPx) % (w - 20);

          // Clear with subtle trail
          ctx.fillStyle = 'rgba(11, 12, 16, 0.32)';
          ctx.fillRect(0, 0, w, h);

          // Subtle scanline grid
          ctx.strokeStyle = 'rgba(255, 255, 255, 0.02)';
          ctx.lineWidth = 1;
          for (let y = 0; y < h; y += 12) {
            ctx.beginPath();
            ctx.moveTo(0, y);
            ctx.lineTo(w, y);
            ctx.stroke();
          }

          // Hue rotation similar to spinner.cpp: hsv_to_rgb(frame * 0.7)
          const hue = (frame * 1.5) % 360;

          // 1. Draw Rotating Smoothness Wheel
          const radius = 46;
          ctx.save();
          ctx.translate(cx, cy);
          ctx.rotate((frame * 0.045) % (Math.PI * 2));

          const petals = 8;
          for (let i = 0; i < petals; i++) {
            const angle = (i * Math.PI * 2) / petals;
            const petalHue = (hue + (i * 360) / petals) % 360;
            ctx.fillStyle = `hsl(${petalHue}, 90%, 60%)`;
            ctx.beginPath();
            ctx.arc(
              Math.cos(angle) * (radius * 0.65),
              Math.sin(angle) * (radius * 0.65),
              radius * 0.28,
              0,
              Math.PI * 2
            );
            ctx.fill();
          }

          // Center core
          ctx.fillStyle = `hsl(${hue}, 95%, 80%)`;
          ctx.beginPath();
          ctx.arc(0, 0, radius * 0.22, 0, Math.PI * 2);
          ctx.fill();
          ctx.restore();

          // 2. Orbiting high-speed motion particles
          const particleCount = 10;
          for (let i = 0; i < particleCount; i++) {
            const pAngle = (frame * 0.035 + (i * Math.PI * 2) / particleCount) % (Math.PI * 2);
            const px = cx + Math.cos(pAngle) * (radius + 24);
            const py = cy + Math.sin(pAngle) * (radius + 24);
            const pHue = (hue + i * 36) % 360;
            ctx.fillStyle = `hsl(${pHue}, 95%, 65%)`;
            ctx.beginPath();
            ctx.arc(px, py, 3, 0, Math.PI * 2);
            ctx.fill();
          }

          // 3. Horizontal Motion Clarity & Judder Test Tracks (Right Side)
          const trackX = cx + radius + 45;
          const trackW = w - trackX - 15;

          if (trackW > 60) {
            // Track Header
            ctx.fillStyle = '#6b7280';
            ctx.font = '9px monospace';
            ctx.fillText('MOTION CLARITY (JUDDER TEST)', trackX, 22);

            // 3 Horizontal Bars with moving blocks
            const barHeights = [36, 72, 108];
            const speeds = [speedPx * 0.7, speedPx * 1.0, speedPx * 1.5];
            const labels = ['1x Speed', '2x Speed', '3x Speed'];

            barHeights.forEach((by, idx) => {
              // Bar background track
              ctx.fillStyle = '#161822';
              ctx.fillRect(trackX, by, trackW, 22);
              ctx.strokeStyle = '#282b3a';
              ctx.strokeRect(trackX, by, trackW, 22);

              // Moving block
              const blockPos = (frame * speeds[idx]) % (trackW - 28);
              ctx.fillStyle = idx === 0 ? '#38bdf8' : idx === 1 ? '#34d399' : '#a78bfa';
              ctx.fillRect(trackX + blockPos, by + 2, 28, 18);

              // Vertical alignment line on moving block
              ctx.fillStyle = '#ffffff';
              ctx.fillRect(trackX + blockPos + 13, by + 2, 2, 18);

              ctx.fillStyle = '#9ca3af';
              ctx.font = '8px monospace';
              ctx.fillText(labels[idx], trackX + 4, by + 14);
            });
          }

          // 4. Latent Sync Top Bezel Tearline Parking Indicator
          if (mode === PacerMode.LatencyFirst) {
            ctx.fillStyle = 'rgba(6, 182, 212, 0.6)';
            ctx.fillRect(0, 0, w, 3);
          }
        }
      }

      animFrameIdRef.current = requestAnimationFrame(renderLoop);
    };

    animFrameIdRef.current = requestAnimationFrame(renderLoop);

    return () => {
      if (animFrameIdRef.current) cancelAnimationFrame(animFrameIdRef.current);
    };
  }, [isRunning, workloadComplexity, simulateSpike, motionSpeed, mode, onPaceFrame]);

  return (
    <div className="w-full bg-[#16171b] rounded-lg border border-[#2b2d35] shadow-[0_8px_30px_rgba(0,0,0,0.5)] overflow-hidden font-sans select-none">
      {/* Game Window Title Bar */}
      <div className="flex items-center justify-between px-3 py-2 bg-[#101114] border-b border-[#23252c]">
        <div className="flex items-center space-x-2">
          <div className="w-3.5 h-3.5 rounded bg-emerald-500/20 border border-emerald-400/50 flex items-center justify-center text-[9px] text-emerald-400 font-bold">
            🎮
          </div>
          <span className="text-xs font-semibold text-zinc-100 tracking-wide">
            spinner.exe (D3D11 / 64-bit) — Motion & Raster Target
          </span>
        </div>
        <div className="flex items-center space-x-2">
          <div className="flex items-center bg-[#181a20] border border-[#282b34] rounded p-0.5 text-[10px] font-mono">
            {(['slow', 'normal', 'fast'] as const).map((s) => (
              <button
                key={s}
                onClick={() => setMotionSpeed(s)}
                className={`px-1.5 py-0.5 rounded capitalize transition-colors ${
                  motionSpeed === s ? 'bg-cyan-500 text-zinc-950 font-bold' : 'text-zinc-400 hover:text-zinc-200'
                }`}
              >
                {s}
              </button>
            ))}
          </div>

          <button
            onClick={() => setIsRunning(!isRunning)}
            className="p-1 rounded bg-[#1e2026] hover:bg-[#282a33] text-zinc-200 text-xs flex items-center space-x-1 px-2 border border-[#2e313a] transition-colors"
          >
            {isRunning ? (
              <>
                <Pause className="w-3 h-3 text-amber-400" />
                <span>Pause</span>
              </>
            ) : (
              <>
                <Play className="w-3 h-3 text-emerald-400" />
                <span>Resume</span>
              </>
            )}
          </button>
        </div>
      </div>

      {/* Render Canvas */}
      <div className="p-3.5 space-y-3">
        <div className="relative rounded bg-[#090a0d] border border-[#1e2026] overflow-hidden flex items-center justify-center">
          <canvas
            ref={canvasRef}
            width={520}
            height={160}
            className="w-full h-[160px] block"
          />

          {/* In-Game DX11 OSD Overlay */}
          <div className="absolute top-2.5 left-3 bg-black/80 backdrop-blur-sm border border-white/10 rounded px-2.5 py-1 text-left font-mono text-[11px] text-zinc-200 space-y-0.5 pointer-events-none">
            <div className="flex items-center space-x-2">
              <span className="text-zinc-400">D3D11:</span>
              <span className="font-bold text-emerald-400">
                {telemetry.currentFps.toFixed(1)} FPS
              </span>
              <span className="text-zinc-500">/</span>
              <span className="text-zinc-300 font-medium">
                {targetFps > 0 ? `${targetFps} FPS` : 'Uncapped'}
              </span>
            </div>
            <div className="text-zinc-400 text-[10px]">
              Frametime: <span className="text-cyan-300 font-semibold">{telemetry.currentFrametimeMs.toFixed(3)} ms</span>
            </div>
            {telemetry.gpuRenderDurationMs !== undefined && (
              <div className="text-zinc-400 text-[9.5px]">
                GPU Time: <span className="text-emerald-300 font-semibold">{telemetry.gpuRenderDurationMs.toFixed(2)} ms</span>
                <span className="text-zinc-500 ml-1.5">(DX11 Disjoint)</span>
              </div>
            )}
          </div>

          {/* Latent Sync Tearline Parking & Composition Tier */}
          <div className="absolute bottom-2.5 right-3 bg-black/80 backdrop-blur-sm border border-white/10 rounded px-2 py-0.5 text-right font-mono text-[10px] text-zinc-300 flex items-center space-x-1.5 pointer-events-none">
            <ShieldCheck className="w-3.5 h-3.5 text-cyan-400" />
            <span>
              {mode === PacerMode.LatencyFirst
                ? 'Tearline: Parked (Top Bezel)'
                : mode === PacerMode.DisplayLocked
                ? 'Console VBI: Synchronized'
                : mode === PacerMode.VrrLive
                ? 'VRR Live: Instant Scanout (0ms)'
                : 'Async: 64-bit Zero-Drift'}
            </span>
            <span className="text-zinc-500">•</span>
            <span className="text-emerald-400 font-semibold">
              {telemetry.compositionTier || 'DirectFlip'}
            </span>
            {mode === PacerMode.VrrLive ? (
              <>
                <span className="text-zinc-500">•</span>
                <span className="text-cyan-300">VRR (-3 Cap: {telemetry.vrrRecommendedCapFps || 141} FPS)</span>
                <span className="text-zinc-500">•</span>
                <span className="text-emerald-300">Anti-Flicker Active</span>
              </>
            ) : mode === PacerMode.Async ? (
              <>
                <span className="text-zinc-500">•</span>
                <span className="text-emerald-300">Drift: 0.000 ms</span>
                <span className="text-zinc-500">•</span>
                <span className="text-cyan-300">Decoupled</span>
              </>
            ) : (
              telemetry.vrrSupported && (
                <>
                  <span className="text-zinc-500">•</span>
                  <span className="text-cyan-300">VRR Cap: {telemetry.vrrRecommendedCapFps || 141} FPS</span>
                </>
              )
            )}
          </div>
        </div>

        {/* Realtime Stress Controls */}
        <div className="flex flex-wrap items-center justify-between gap-2 pt-1 text-xs">
          <div className="flex items-center space-x-3">
            <div className="flex items-center space-x-1.5 text-zinc-300">
              <Flame className="w-3.5 h-3.5 text-amber-400" />
              <span>Simulated Load:</span>
            </div>
            <input
              type="range"
              min="1"
              max="5"
              step="1"
              value={workloadComplexity}
              onChange={(e) => setWorkloadComplexity(parseInt(e.target.value))}
              className="w-24 accent-cyan-500 bg-[#1e2026] h-1.5 rounded cursor-pointer"
            />
            <span className="font-mono text-zinc-400 text-xs">
              L{workloadComplexity}
            </span>
          </div>

          <div className="flex items-center space-x-2">
            {onInjectHitch && (
              <button
                onClick={() => onInjectHitch(48)}
                className="px-2.5 py-1 rounded text-xs bg-red-950/40 border border-red-500/40 text-red-300 hover:bg-red-900/60 transition-colors font-medium"
                title="Simulate 48ms asset streaming hitch to test zero-ringing anti-windup recovery"
              >
                Inject 48ms Hitch
              </button>
            )}
            <button
              onClick={() => setSimulateSpike(!simulateSpike)}
              className={`px-2.5 py-1 rounded text-xs border transition-colors ${
                simulateSpike
                  ? 'bg-amber-950/70 border-amber-500/60 text-amber-300 font-medium'
                  : 'bg-[#1e2026] border-[#2e313a] text-zinc-400 hover:text-zinc-200'
              }`}
            >
              {simulateSpike ? 'Spikes Active (14ms)' : 'Inject Spikes'}
            </button>
          </div>
        </div>
      </div>
    </div>
  );
};

