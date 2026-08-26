import React, { useState, useEffect } from 'react';
import { PacerMode, PacerState, PacerApi, ProcessTarget } from '../types';
import {
  Menu,
  ChevronDown,
  Pin,
  Minus,
  X,
  Play,
  Check,
  Cpu,
  Monitor,
  Activity,
  Sliders,
  Sparkles,
  Zap,
} from 'lucide-react';

interface Props {
  targetFps: number;
  onApplyFps: (fps: number) => void;
  mode: PacerMode;
  onModeChange: (mode: PacerMode) => void;
  delayBias: number;
  onDelayBiasChange: (bias: number) => void;
  selectedProcess: ProcessTarget;
  availableProcesses: ProcessTarget[];
  onSelectProcess: (proc: ProcessTarget) => void;
  liveFrametimeMs: number;
  liveFps: number;
  onToggleGraph: () => void;
  isGraphOpen: boolean;
  ringData?: number[];
  pllPhaseUs?: number;
  displayRefreshHz?: number;
  onDisplayRefreshChange?: (hz: number) => void;
  divisorRatioLabel?: string;
  isDisplayDivisor?: boolean;
  isStutterWarning?: boolean;
  stutterReason?: string;
  renderHeadroomMs?: number;
  autoSnapNotification?: {
    originalFps: number;
    snappedFps: number;
    ratioLabel: string;
    refreshHz: number;
    reason: string;
    timestamp: number;
  };
  onClearAutoSnapNotification?: () => void;
}

export const PacerNativeWindow: React.FC<Props> = ({
  targetFps,
  onApplyFps,
  mode,
  onModeChange,
  delayBias,
  onDelayBiasChange,
  selectedProcess,
  availableProcesses,
  onSelectProcess,
  liveFrametimeMs,
  liveFps,
  onToggleGraph,
  isGraphOpen,
  ringData = [],
  pllPhaseUs = 0.0,
  displayRefreshHz = 144.0,
  onDisplayRefreshChange,
  divisorRatioLabel = '',
  isDisplayDivisor = true,
  isStutterWarning = false,
  stutterReason = '',
  renderHeadroomMs = 13.5,
  autoSnapNotification,
  onClearAutoSnapNotification,
}) => {
  const [inputVal, setInputVal] = useState<string>(targetFps.toString());
  const [isPinned, setIsPinned] = useState<boolean>(true);
  const [showMenu, setShowMenu] = useState<boolean>(false);
  const [showAppsMenu, setShowAppsMenu] = useState<boolean>(false);
  const [processSearch, setProcessSearch] = useState<string>('');
  const [showBiasSubmenu, setShowBiasSubmenu] = useState<boolean>(false);
  const [showModeSubmenu, setShowModeSubmenu] = useState<boolean>(false);
  const [showFpsSubmenu, setShowFpsSubmenu] = useState<boolean>(false);
  const [showRefreshSubmenu, setShowRefreshSubmenu] = useState<boolean>(false);
  const [startWithWindows, setStartWithWindows] = useState<boolean>(true);
  const [minimizeToTray, setMinimizeToTray] = useState<boolean>(true);
  const [isAppliedAnimation, setIsAppliedAnimation] = useState<boolean>(false);

  useEffect(() => {
    setInputVal(targetFps === 0 ? '0' : targetFps.toFixed(targetFps % 1 === 0 ? 0 : 2));
  }, [targetFps]);

  const handleApply = (e?: React.FormEvent) => {
    if (e) e.preventDefault();
    const parsed = parseFloat(inputVal);
    const validFps = isNaN(parsed) ? 60 : Math.max(0, Math.min(1000, parsed));
    onApplyFps(validFps);
    setIsAppliedAnimation(true);
    setTimeout(() => setIsAppliedAnimation(false), 700);
  };

  const getModeLabel = (m: PacerMode) => {
    switch (m) {
      case PacerMode.LatencyFirst:
        return 'Special K Latent Sync';
      case PacerMode.DisplayLocked:
        return 'Console Smoothness';
      case PacerMode.VrrLive:
        return 'VRR Live';
      case PacerMode.Async:
        return 'Async (Compat)';
      default:
        return 'Special K Latent Sync';
    }
  };

  const getApiLabel = (api: PacerApi, is64: boolean) => {
    const bitStr = is64 ? '(64-bit)' : '(32-bit)';
    switch (api) {
      case PacerApi.Dxgi:
        return `D3D11 ${bitStr}`;
      case PacerApi.D3D9:
        return `D3D9 ${bitStr}`;
      case PacerApi.Vulkan:
        return `Vulkan ${bitStr}`;
      case PacerApi.OpenGL:
        return `OpenGL ${bitStr}`;
      default:
        return `DirectX ${bitStr}`;
    }
  };

  const statusLine1 = selectedProcess.pid
    ? `Observing (${selectedProcess.name} / ${getApiLabel(selectedProcess.api, selectedProcess.is64Bit)})`
    : `Observing (no game presenting)`;

  const biasPercent = Math.round(delayBias * 100);
  const statusLine2 = `${getModeLabel(mode)} @ ${targetFps > 0 ? `${targetFps.toFixed(0)} FPS` : 'Uncapped'}${
    mode === PacerMode.LatencyFirst ? ` (Bias: ${biasPercent}%)` : ''
  }`;

  return (
    <div className="w-full max-w-[380px] bg-[#16171b] text-[#e0e0e0] rounded-lg border border-[#2b2d35] shadow-[0_12px_40px_rgba(0,0,0,0.65)] overflow-hidden font-sans select-none ring-1 ring-white/5">
      {/* Windows 11 Fluent Dark Title Bar */}
      <div className="flex items-center justify-between px-3 py-2 bg-[#101114] border-b border-[#23252c]">
        <div className="flex items-center space-x-2">
          <div className="w-4 h-4 rounded bg-cyan-500/20 border border-cyan-400/50 flex items-center justify-center text-[10px] text-cyan-400 font-bold font-mono">
            FP
          </div>
          <span className="text-xs font-semibold tracking-wide text-zinc-100">
            Framepacer
          </span>
          <span className="w-1.5 h-1.5 rounded-full bg-emerald-400 animate-pulse" title="Service Hook Active" />
        </div>

        <div className="flex items-center space-x-0.5">
          <button
            id="pacer-win-pin-btn"
            onClick={() => setIsPinned(!isPinned)}
            className={`p-1.5 rounded text-xs transition-colors ${
              isPinned ? 'text-cyan-400 bg-cyan-950/60' : 'text-zinc-400 hover:text-zinc-200 hover:bg-[#202228]'
            }`}
            title="Always on Top"
          >
            <Pin className="w-3.5 h-3.5" />
          </button>
          <button
            id="pacer-win-min-btn"
            className="p-1.5 rounded text-zinc-400 hover:text-zinc-200 hover:bg-[#202228] text-xs"
            title="Minimize to Tray"
          >
            <Minus className="w-3.5 h-3.5" />
          </button>
          <button
            id="pacer-win-close-btn"
            className="p-1.5 rounded text-zinc-400 hover:text-red-300 hover:bg-red-950/50 text-xs"
            title="Close"
          >
            <X className="w-3.5 h-3.5" />
          </button>
        </div>
      </div>

      {/* Main Controls Area */}
      <div className="p-3.5 space-y-3">
        {/* Upper Toolbar: Hamburger Menu, Apps Dropdown, Graph Toggle */}
        <div className="flex items-center justify-between relative">
          <div className="flex items-center space-x-2">
            {/* Hamburger Button */}
            <div className="relative">
              <button
                id="pacer-hamburger-btn"
                onClick={() => {
                  setShowMenu(!showMenu);
                  setShowAppsMenu(false);
                }}
                className={`p-1.5 rounded border transition-colors ${
                  showMenu
                    ? 'bg-cyan-950/70 border-cyan-500/60 text-cyan-300'
                    : 'bg-[#1e2026] border-[#2e313a] hover:bg-[#282a33] text-zinc-200'
                }`}
                title="Pacing Settings & Presets"
              >
                <Menu className="w-4 h-4" />
              </button>

              {/* Hamburger Popup Menu */}
              {showMenu && (
                <div className="absolute left-0 top-9 w-64 bg-[#141519] border border-[#30333d] rounded-md shadow-2xl py-1.5 z-50 text-xs text-zinc-200 space-y-0.5 animate-in fade-in zoom-in-95 duration-100">
                  {/* Display Refresh Rate Selector */}
                  <div
                    className="px-3 py-1.5 hover:bg-[#242730] flex items-center justify-between cursor-pointer"
                    onMouseEnter={() => setShowRefreshSubmenu(true)}
                  >
                    <span>Monitor Refresh Rate</span>
                    <span className="text-[10px] text-zinc-500 font-mono">▶</span>
                  </div>
                  {showRefreshSubmenu && onDisplayRefreshChange && (
                    <div
                      className="absolute left-60 top-0 w-44 bg-[#141519] border border-[#30333d] rounded-md shadow-2xl py-1 z-50"
                      onMouseLeave={() => setShowRefreshSubmenu(false)}
                    >
                      {[60, 120, 144, 165, 240, 360].map((hz) => (
                        <div
                          key={hz}
                          onClick={() => {
                            onDisplayRefreshChange(hz);
                            setShowMenu(false);
                            setShowRefreshSubmenu(false);
                          }}
                          className="px-3 py-1 hover:bg-[#242730] cursor-pointer flex items-center justify-between"
                        >
                          <span>{hz} Hz Display</span>
                          {Math.round(displayRefreshHz) === hz && (
                            <Check className="w-3 h-3 text-cyan-400" />
                          )}
                        </div>
                      ))}
                    </div>
                  )}

                  {/* Preset FPS */}
                  <div
                    className="px-3 py-1.5 hover:bg-[#242730] flex items-center justify-between cursor-pointer"
                    onMouseEnter={() => setShowFpsSubmenu(true)}
                  >
                    <span>Target FPS Presets</span>
                    <span className="text-[10px] text-zinc-500 font-mono">▶</span>
                  </div>
                  {showFpsSubmenu && (
                    <div
                      className="absolute left-60 top-7 w-48 bg-[#141519] border border-[#30333d] rounded-md shadow-2xl py-1 z-50"
                      onMouseLeave={() => setShowFpsSubmenu(false)}
                    >
                      {[
                        { fps: 0, label: '0 (Uncapped)' },
                        { fps: Math.round(displayRefreshHz), label: `${Math.round(displayRefreshHz)} FPS (1:1 Native)` },
                        { fps: Math.round(displayRefreshHz / 2), label: `${Math.round(displayRefreshHz / 2)} FPS (1:2 Divisor)` },
                        { fps: Math.round(displayRefreshHz / 3), label: `${Math.round(displayRefreshHz / 3)} FPS (1:3 Divisor)` },
                        { fps: 60, label: '60 FPS' },
                        { fps: 120, label: '120 FPS' },
                        { fps: 144, label: '144 FPS' },
                      ].map((item) => (
                        <div
                          key={item.fps}
                          onClick={() => {
                            onApplyFps(item.fps);
                            setShowMenu(false);
                            setShowFpsSubmenu(false);
                          }}
                          className="px-3 py-1 hover:bg-[#242730] cursor-pointer flex items-center justify-between"
                        >
                          <span>{item.label}</span>
                          {targetFps === item.fps && <Check className="w-3 h-3 text-cyan-400" />}
                        </div>
                      ))}
                    </div>
                  )}

                  {/* Pacing Mode */}
                  <div
                    className="px-3 py-1.5 hover:bg-[#242730] flex items-center justify-between cursor-pointer"
                    onMouseEnter={() => setShowModeSubmenu(true)}
                  >
                    <div className="flex items-center space-x-1.5">
                      <span>Pacing Mode</span>
                    </div>
                    <span className="text-[10px] text-zinc-500 font-mono">▶</span>
                  </div>
                  {showModeSubmenu && (
                    <div
                      className="absolute left-60 top-7 w-60 bg-[#141519] border border-[#30333d] rounded-md shadow-2xl py-1 z-50"
                      onMouseLeave={() => setShowModeSubmenu(false)}
                    >
                      {[
                        { id: PacerMode.LatencyFirst, label: 'Special K Latent Sync (Default)' },
                        { id: PacerMode.DisplayLocked, label: 'Console Smoothness (VBI-PLL)' },
                        { id: PacerMode.VrrLive, label: 'VRR Live (G-Sync/FreeSync)' },
                        { id: PacerMode.Async, label: 'Async (Compat / RTSS Style)' },
                      ].map((item) => (
                        <div
                          key={item.id}
                          onClick={() => {
                            onModeChange(item.id);
                            setShowMenu(false);
                            setShowModeSubmenu(false);
                          }}
                          className="px-3 py-1.5 hover:bg-[#242730] cursor-pointer flex items-center justify-between"
                        >
                          <span className="truncate">{item.label}</span>
                          {mode === item.id && <Check className="w-3.5 h-3.5 text-cyan-400 ml-2 shrink-0" />}
                        </div>
                      ))}
                    </div>
                  )}

                  {/* Delay Bias */}
                  <div
                    className={`px-3 py-1.5 flex items-center justify-between ${
                      mode === PacerMode.LatencyFirst
                        ? 'hover:bg-[#242730] cursor-pointer text-zinc-200'
                        : 'text-zinc-600 cursor-not-allowed'
                    }`}
                    onMouseEnter={() => mode === PacerMode.LatencyFirst && setShowBiasSubmenu(true)}
                  >
                    <span>Latent Sync Delay Bias</span>
                    <span className="text-[10px] text-zinc-500 font-mono">▶</span>
                  </div>
                  {showBiasSubmenu && mode === PacerMode.LatencyFirst && (
                    <div
                      className="absolute left-60 top-14 w-48 bg-[#141519] border border-[#30333d] rounded-md shadow-2xl py-1 z-50"
                      onMouseLeave={() => setShowBiasSubmenu(false)}
                    >
                      {[
                        { val: 0.0, label: '0% (Max Smooth / Safe)' },
                        { val: 0.25, label: '25% Bias' },
                        { val: 0.5, label: '50% Balanced' },
                        { val: 0.75, label: '75% Low Latency' },
                        { val: 1.0, label: '100% (Sub-frame Lag)' },
                      ].map((item) => (
                        <div
                          key={item.val}
                          onClick={() => {
                            onDelayBiasChange(item.val);
                            setShowMenu(false);
                            setShowBiasSubmenu(false);
                          }}
                          className="px-3 py-1 hover:bg-[#242730] cursor-pointer flex items-center justify-between"
                        >
                          <span>{item.label}</span>
                          {Math.abs(delayBias - item.val) < 0.05 && (
                            <Check className="w-3.5 h-3.5 text-cyan-400" />
                          )}
                        </div>
                      ))}
                    </div>
                  )}

                  <div className="h-[1px] bg-[#282b34] my-1" />

                  <div
                    onClick={() => setStartWithWindows(!startWithWindows)}
                    className="px-3 py-1.5 hover:bg-[#242730] cursor-pointer flex items-center justify-between"
                  >
                    <span>Start with Windows</span>
                    {startWithWindows && <Check className="w-3.5 h-3.5 text-cyan-400" />}
                  </div>

                  <div
                    onClick={() => setMinimizeToTray(!minimizeToTray)}
                    className="px-3 py-1.5 hover:bg-[#242730] cursor-pointer flex items-center justify-between"
                  >
                    <span>Minimize to Tray</span>
                    {minimizeToTray && <Check className="w-3.5 h-3.5 text-cyan-400" />}
                  </div>

                  <div
                    onClick={() => setIsPinned(!isPinned)}
                    className="px-3 py-1.5 hover:bg-[#242730] cursor-pointer flex items-center justify-between"
                  >
                    <span>Always on Top</span>
                    {isPinned && <Check className="w-3.5 h-3.5 text-cyan-400" />}
                  </div>

                  <div className="h-[1px] bg-[#282b34] my-1" />

                  <div
                    onClick={() => {
                      onApplyFps(0);
                      setShowMenu(false);
                    }}
                    className="px-3 py-1.5 hover:bg-[#242730] cursor-pointer text-amber-300"
                  >
                    Reset / Eject Core Hooks
                  </div>
                </div>
              )}
            </div>

            {/* Apps Dropdown */}
            <div className="relative">
              <button
                id="pacer-apps-dropdown-btn"
                onClick={() => {
                  setShowAppsMenu(!showAppsMenu);
                  setShowMenu(false);
                }}
                className="flex items-center space-x-1.5 px-2.5 py-1.5 bg-[#1e2026] hover:bg-[#282a33] border border-[#2e313a] rounded text-xs text-zinc-100 transition-colors"
              >
                <Cpu className="w-3.5 h-3.5 text-cyan-400" />
                <span className="truncate max-w-[130px] font-medium">{selectedProcess.name}</span>
                <ChevronDown className="w-3 h-3 text-zinc-400" />
              </button>

              {showAppsMenu && (
                <div className="absolute left-0 top-9 w-72 bg-[#141519] border border-[#30333d] rounded-md shadow-2xl py-1.5 z-50 text-xs text-zinc-200 animate-in fade-in zoom-in-95 duration-100">
                  <div className="px-3 py-1 flex items-center justify-between border-b border-[#242730] pb-1.5 mb-1">
                    <span className="text-[10px] text-zinc-400 font-bold uppercase tracking-wider">
                      Detected 3D Games ({availableProcesses.length})
                    </span>
                    <span className="text-[9px] text-emerald-400 font-mono flex items-center gap-1">
                      <span className="w-1.5 h-1.5 rounded-full bg-emerald-400 animate-pulse" />
                      Live Hook
                    </span>
                  </div>

                  {/* Search Bar for Games */}
                  <div className="px-2 py-1">
                    <input
                      type="text"
                      placeholder="Filter running games..."
                      value={processSearch}
                      onChange={(e) => setProcessSearch(e.target.value)}
                      className="w-full px-2 py-1 bg-[#0d0e12] border border-[#282b34] focus:border-cyan-500 rounded text-[11px] text-zinc-200 placeholder-zinc-500 focus:outline-none"
                    />
                  </div>

                  <div className="max-h-56 overflow-y-auto">
                    {availableProcesses
                      .filter(
                        (p) =>
                          p.name.toLowerCase().includes(processSearch.toLowerCase()) ||
                          (p.windowTitle && p.windowTitle.toLowerCase().includes(processSearch.toLowerCase())) ||
                          (p.engine && p.engine.toLowerCase().includes(processSearch.toLowerCase()))
                      )
                      .map((proc) => (
                        <div
                          key={proc.pid}
                          onClick={() => {
                            onSelectProcess(proc);
                            setShowAppsMenu(false);
                          }}
                          className={`px-3 py-1.5 hover:bg-[#242730] cursor-pointer flex items-center justify-between transition-colors ${
                            selectedProcess.pid === proc.pid ? 'bg-cyan-950/40 border-l-2 border-cyan-400' : ''
                          }`}
                        >
                          <div className="flex flex-col min-w-0 pr-2">
                            <span className="font-medium text-zinc-100 truncate">{proc.name}</span>
                            <span className="text-[10px] text-zinc-400 truncate">
                              {proc.windowTitle || `PID ${proc.pid}`}
                            </span>
                            <div className="flex items-center gap-1.5 mt-0.5">
                              <span className="text-[9px] px-1 rounded bg-zinc-800 text-cyan-300 font-mono">
                                {getApiLabel(proc.api, proc.is64Bit)}
                              </span>
                              {proc.engine && (
                                <span className="text-[9px] text-zinc-500 truncate max-w-[120px]">
                                  {proc.engine}
                                </span>
                              )}
                            </div>
                          </div>
                          {selectedProcess.pid === proc.pid && (
                            <Check className="w-4 h-4 text-cyan-400 shrink-0" />
                          )}
                        </div>
                      ))}
                  </div>
                </div>
              )}
            </div>
          </div>

          {/* Oscilloscope Graph Toggle Button */}
          <button
            id="pacer-graph-toggle-btn"
            onClick={onToggleGraph}
            className={`flex items-center space-x-1 px-2.5 py-1.5 rounded border text-xs transition-colors ${
              isGraphOpen
                ? 'bg-cyan-950/70 border-cyan-500/60 text-cyan-300 font-medium'
                : 'bg-[#1e2026] border-[#2e313a] text-zinc-400 hover:text-zinc-200'
            }`}
            title="Toggle Oscilloscope Frametime Graph"
          >
            <Activity className="w-3.5 h-3.5 text-cyan-400" />
            <span>Graph</span>
          </button>
        </div>

        {/* Target FPS Input and Apply Button */}
        <form onSubmit={handleApply} className="flex items-center space-x-2">
          <div className="relative flex-1">
            <input
              id="pacer-target-fps-input"
              type="text"
              value={inputVal}
              onChange={(e) => setInputVal(e.target.value)}
              placeholder="e.g. 72 or 0"
              className="w-full bg-[#101114] border border-[#2e313a] focus:border-cyan-500 focus:outline-none rounded px-3 py-1.5 text-sm font-mono text-zinc-100 placeholder-zinc-600 shadow-inner"
            />
            <div className="absolute right-2.5 top-2 text-[11px] text-zinc-500 font-mono">
              FPS
            </div>
          </div>

          <button
            id="pacer-apply-btn"
            type="submit"
            className={`px-4 py-1.5 rounded font-medium text-xs tracking-wide transition-all shadow-sm ${
              isAppliedAnimation
                ? 'bg-emerald-500 text-zinc-950 font-bold scale-95'
                : 'bg-cyan-500 hover:bg-cyan-400 text-zinc-950 font-bold active:scale-95'
            }`}
          >
            {isAppliedAnimation ? 'Applied!' : 'Apply'}
          </button>
        </form>

        {/* Auto-Snap Explanation Notification Banner */}
        {autoSnapNotification && mode === PacerMode.LatencyFirst && (
          <div className="bg-cyan-950/40 border border-cyan-500/50 rounded p-2.5 text-xs space-y-1.5 animate-in fade-in duration-200 shadow-lg">
            <div className="flex items-center justify-between">
              <div className="flex items-center space-x-1.5 text-cyan-300 font-bold text-xs">
                <Zap className="w-3.5 h-3.5 text-cyan-400 shrink-0" />
                <span>Auto-Snapped to {autoSnapNotification.snappedFps} FPS</span>
              </div>
              {onClearAutoSnapNotification && (
                <button
                  type="button"
                  onClick={onClearAutoSnapNotification}
                  className="text-zinc-400 hover:text-zinc-100 text-[10px] px-1 py-0.5 rounded hover:bg-white/10 transition-colors"
                  title="Dismiss notice"
                >
                  ✕
                </button>
              )}
            </div>
            <p className="text-[11px] text-zinc-200 leading-relaxed font-sans">
              {autoSnapNotification.reason}
            </p>
            <div className="flex flex-wrap items-center gap-1.5 pt-1">
              <span className="text-[10px] text-zinc-400 font-mono">Cadence Options:</span>
              {[
                { label: `1:1 (${Math.round(displayRefreshHz)} FPS)`, fps: Math.round(displayRefreshHz) },
                { label: `1:2 (${Math.round(displayRefreshHz / 2)} FPS)`, fps: Math.round(displayRefreshHz / 2) },
                { label: `1:3 (${Math.round(displayRefreshHz / 3)} FPS)`, fps: Math.round(displayRefreshHz / 3) },
              ].map((c) => (
                <button
                  key={c.fps}
                  type="button"
                  onClick={() => onApplyFps(c.fps)}
                  className={`px-2 py-0.5 rounded text-[10px] font-mono font-bold transition-colors ${
                    targetFps === c.fps
                      ? 'bg-cyan-500 text-zinc-950 shadow'
                      : 'bg-zinc-800 hover:bg-zinc-700 text-zinc-200 border border-zinc-700'
                  }`}
                >
                  {c.label}
                </button>
              ))}
            </div>
          </div>
        )}

        {/* Stutter Diagnostic & Cadence Alignment Banner (for non-LatentSync or manual overrides) */}
        {!autoSnapNotification && isStutterWarning && mode === PacerMode.LatencyFirst && targetFps > 0 && (
          <div className="bg-amber-950/40 border border-amber-500/40 rounded p-2 text-xs space-y-1.5 animate-in fade-in duration-200">
            <div className="flex items-start space-x-1.5 text-amber-300">
              <span className="font-bold text-[11px]">⚠ Stutter Cause Detected:</span>
            </div>
            <p className="text-[10px] text-zinc-300 leading-relaxed">
              Target <strong className="text-amber-200">{targetFps} FPS</strong> is not a divisor of your{' '}
              <strong className="text-amber-200">{Math.round(displayRefreshHz)} Hz</strong> display. Latent Sync requires exact cadence alignment to park the tearline off-screen and eliminate 3:2 pulldown judder.
            </p>
            <div className="flex flex-wrap items-center gap-1.5 pt-0.5">
              <button
                type="button"
                onClick={() => onApplyFps(Math.round(displayRefreshHz / 2))}
                className="px-2 py-0.5 rounded bg-cyan-500 hover:bg-cyan-400 text-zinc-950 font-bold text-[10px] shadow transition-colors"
              >
                ⚡ Snap to {Math.round(displayRefreshHz / 2)} FPS (1/2 Cadence)
              </button>
              <button
                type="button"
                onClick={() => onModeChange(PacerMode.VrrLive)}
                className="px-2 py-0.5 rounded bg-zinc-800 hover:bg-zinc-700 text-cyan-300 border border-cyan-500/30 font-medium text-[10px] transition-colors"
              >
                Switch to VRR Live
              </button>
            </div>
          </div>
        )}

        {/* Quick Mode Switcher Tabs */}
        <div className="grid grid-cols-4 gap-1 p-1 bg-[#101114] border border-[#23252c] rounded text-[10px]">
          {[
            { id: PacerMode.LatencyFirst, label: 'Latent Sync' },
            { id: PacerMode.DisplayLocked, label: 'Console' },
            { id: PacerMode.VrrLive, label: 'VRR Live' },
            { id: PacerMode.Async, label: 'Async' },
          ].map((m) => (
            <button
              key={m.id}
              type="button"
              onClick={() => onModeChange(m.id)}
              className={`py-1 rounded font-medium transition-colors text-center ${
                mode === m.id
                  ? 'bg-cyan-500 text-zinc-950 font-bold shadow-sm'
                  : 'text-zinc-400 hover:text-zinc-200 hover:bg-[#1c1e25]'
              }`}
            >
              {m.label}
            </button>
          ))}
        </div>

        {/* Latent Sync Direct Delay Bias Bar (Active only in Latent Sync mode) */}
        {mode === PacerMode.LatencyFirst && (
          <div className="bg-[#101114] border border-cyan-900/40 rounded p-2.5 space-y-2 text-xs">
            <div className="flex items-center justify-between">
              <div className="flex items-center space-x-1.5">
                <span className="text-zinc-300 font-semibold text-[11px]">Latent Sync Delay Bias:</span>
                <span className="text-cyan-400 font-mono font-bold text-[11px]">{Math.round(delayBias * 100)}%</span>
              </div>
              <span className="text-[10px] px-1.5 py-0.5 rounded bg-emerald-950/60 border border-emerald-500/40 text-emerald-400 font-mono">
                {isDisplayDivisor ? 'Tearline Parked Off-Screen' : 'Non-Divisor Alignment'}
              </span>
            </div>

            {/* Bias slider and preset quick-buttons */}
            <div className="space-y-1.5">
              <input
                type="range"
                min="0"
                max="100"
                step="5"
                value={Math.round(delayBias * 100)}
                onChange={(e) => onDelayBiasChange(Number(e.target.value) / 100)}
                className="w-full h-1.5 bg-[#252830] rounded-lg appearance-none cursor-pointer accent-cyan-400"
              />
              <div className="flex items-center justify-between text-[10px] text-zinc-400">
                <span>0% (Max Stability)</span>
                <div className="flex items-center space-x-1">
                  {[0.0, 0.25, 0.5, 0.75, 1.0].map((b) => (
                    <button
                      key={b}
                      type="button"
                      onClick={() => onDelayBiasChange(b)}
                      className={`px-1.5 py-0.5 rounded font-mono ${
                        Math.abs(delayBias - b) < 0.04
                          ? 'bg-cyan-500/30 text-cyan-300 border border-cyan-500/60 font-bold'
                          : 'bg-[#1b1c22] text-zinc-500 hover:text-zinc-300'
                      }`}
                    >
                      {Math.round(b * 100)}%
                    </button>
                  ))}
                </div>
                <span>100% (Min Lag)</span>
              </div>
            </div>
          </div>
        )}

        {/* Status Line 1 & Line 2 */}
        <div className="bg-[#101114] border border-[#23252c] rounded p-2.5 space-y-1.5 text-xs font-mono">
          <div className="flex items-center justify-between text-zinc-400">
            <div className="flex items-center space-x-1.5 truncate max-w-[210px]" title={statusLine1}>
              <span className="w-2 h-2 rounded-full bg-emerald-400 animate-pulse shrink-0" />
              <span className="truncate">{statusLine1}</span>
            </div>
            <span className="text-[11px] text-zinc-300 font-semibold">
              {liveFps > 0 ? `${liveFps.toFixed(1)} FPS` : '0 FPS'}
            </span>
          </div>

          <div className="flex items-center justify-between">
            <span className="text-cyan-300 font-medium truncate text-[11px]" title={statusLine2}>
              {statusLine2}
            </span>
            <span className="text-emerald-400 font-bold">
              {liveFrametimeMs > 0 ? `${liveFrametimeMs.toFixed(2)} ms` : '--'}
            </span>
          </div>

          {/* Cadence & Adaptive Guard Telemetry */}
          <div className="pt-1 border-t border-[#1f2128] flex items-center justify-between text-[10px] text-zinc-400">
            <div className="flex items-center space-x-1">
              <span className="text-zinc-500">Cadence:</span>
              <span className={`font-semibold ${isDisplayDivisor ? 'text-emerald-400' : 'text-amber-400'}`}>
                {divisorRatioLabel || (isDisplayDivisor ? 'Locked 1:1' : 'Non-Divisor')}
              </span>
            </div>
            <div className="flex items-center space-x-1" title="Remaining render headroom before VBlank deadline">
              <span className="text-zinc-500">Headroom:</span>
              <span className={`font-semibold ${renderHeadroomMs > 3 ? 'text-cyan-300' : 'text-amber-400'}`}>
                {renderHeadroomMs.toFixed(1)} ms
              </span>
            </div>
          </div>
        </div>

        {/* Quick Mode & Presets Bar */}
        <div className="flex items-center justify-between pt-1 border-t border-[#23252c] text-[11px]">
          <span className="text-zinc-500">Presets:</span>
          <div className="flex items-center space-x-1 font-mono">
            {[0, Math.round(displayRefreshHz / 3), Math.round(displayRefreshHz / 2), Math.round(displayRefreshHz)].map((fps) => (
              <button
                key={fps}
                onClick={() => onApplyFps(fps)}
                className={`px-2 py-0.5 rounded transition-colors ${
                  targetFps === fps
                    ? 'bg-cyan-500/20 text-cyan-300 font-bold border border-cyan-500/40'
                    : 'bg-[#1e2026] text-zinc-400 hover:text-zinc-200 border border-transparent hover:border-[#2e313a]'
                }`}
              >
                {fps === 0 ? 'UNCAP' : `${fps}`}
              </button>
            ))}
          </div>
        </div>
      </div>
    </div>
  );
};

