import React, { useState, useEffect, useRef } from 'react';
import { PacerMode, PacerState, PacerApi, ProcessTarget, PacerTelemetry, SharedMemoryState } from './types';
import { SimulatedPacerEngine } from './utils/pacerEngineSim';
import { PacerNativeWindow } from './components/PacerNativeWindow';
import { FrametimeOscilloscope } from './components/FrametimeOscilloscope';
import { LiveBenchmarkView } from './components/LiveBenchmarkView';
import { SharedMemoryInspector } from './components/SharedMemoryInspector';
import { CodeFixExplainer } from './components/CodeFixExplainer';
import { RepoFileViewer } from './components/RepoFileViewer';
import {
  Search,
  Wifi,
  Volume2,
  ChevronUp,
  Sliders,
  ShieldCheck,
  Zap,
  Layout,
  Database,
  FileCode,
  Sparkles,
} from 'lucide-react';

const AVAILABLE_PROCESSES: ProcessTarget[] = [
  { pid: 14280, name: 'spinner.exe', windowTitle: 'DX11 Precision Render Target', api: PacerApi.Dxgi, is64Bit: true, active: true, isRunning: true, nativeFps: 144, engine: 'DirectX 11' },
  { pid: 9812, name: 'Cyberpunk2077.exe', windowTitle: 'Cyberpunk 2077 (v2.13)', api: PacerApi.Dxgi, is64Bit: true, active: false, isRunning: true, nativeFps: 120, engine: 'REDengine 4 (DX12)' },
  { pid: 18452, name: 'b1-Win64-Shipping.exe', windowTitle: 'Black Myth: Wukong', api: PacerApi.Dxgi, is64Bit: true, active: false, isRunning: true, nativeFps: 144, engine: 'Unreal Engine 5 (DX12)' },
  { pid: 7724, name: 'cs2.exe', windowTitle: 'Counter-Strike 2', api: PacerApi.Vulkan, is64Bit: true, active: false, isRunning: true, nativeFps: 240, engine: 'Source 2 (Vulkan)' },
  { pid: 15308, name: 'eldenring.exe', windowTitle: 'ELDEN RING™', api: PacerApi.Dxgi, is64Bit: true, active: false, isRunning: true, nativeFps: 60, engine: 'Dantelion (DX12)' },
  { pid: 11044, name: 'DOOMEternalx64vk.exe', windowTitle: 'DOOM Eternal', api: PacerApi.Vulkan, is64Bit: true, active: false, isRunning: true, nativeFps: 165, engine: 'id Tech 7 (Vulkan)' },
  { pid: 12940, name: 'VALORANT-Win64-Shipping.exe', windowTitle: 'VALORANT', api: PacerApi.Dxgi, is64Bit: true, active: false, isRunning: true, nativeFps: 240, engine: 'Unreal Engine 4 (DX11)' },
  { pid: 8832, name: 'r5apex.exe', windowTitle: 'Apex Legends', api: PacerApi.Dxgi, is64Bit: true, active: false, isRunning: true, nativeFps: 144, engine: 'Source (DX11)' },
  { pid: 16420, name: 'bg3_dx11.exe', windowTitle: "Baldur's Gate 3", api: PacerApi.Dxgi, is64Bit: true, active: false, isRunning: true, nativeFps: 120, engine: 'Divinity Engine 4.0' },
  { pid: 13856, name: 'ForzaHorizon5.exe', windowTitle: 'Forza Horizon 5', api: PacerApi.Dxgi, is64Bit: true, active: false, isRunning: true, nativeFps: 144, engine: 'ForzaTech (DX12)' },
  { pid: 6540, name: 'hl2.exe', windowTitle: 'Half-Life 2', api: PacerApi.D3D9, is64Bit: false, active: false, isRunning: true, nativeFps: 300, engine: 'Source (D3D9)' },
];

export default function App() {
  const engineRef = useRef<SimulatedPacerEngine>(new SimulatedPacerEngine());
  const [targetFps, setTargetFps] = useState<number>(72.0);
  const [mode, setMode] = useState<PacerMode>(PacerMode.LatencyFirst);
  const [delayBias, setDelayBias] = useState<number>(0.0);
  const [selectedProcess, setSelectedProcess] = useState<ProcessTarget>(AVAILABLE_PROCESSES[0]);
  const [isGraphOpen, setIsGraphOpen] = useState<boolean>(true);
  const [activeTab, setActiveTab] = useState<'workspace' | 'shm' | 'diagnostics'>('workspace');

  // Time state for Windows taskbar clock
  const [currentTimeStr, setCurrentTimeStr] = useState<string>('6:19 AM');
  const [currentDateStr, setCurrentDateStr] = useState<string>('8/26/2026');

  const [telemetry, setTelemetry] = useState<PacerTelemetry>({
    currentFps: 72.0,
    currentFrametimeMs: 13.888,
    meanFrametimeMs: 13.888,
    stdDevMs: 0.0002,
    stdDevUs: 0.2,
    minMs: 13.88,
    maxMs: 13.89,
    p99Ms: 13.89,
    p99_9Ms: 13.89,
    totalFrames: 0,
    isDisplayDivisor: true,
    phaseSteeringUs: 0.0,
    isTearingAllowed: true,
    renderHeadroomMs: 10.5,
    displayRefreshHz: 144,
    divisorRatioLabel: '1/2 Cadence (144 Hz locked)',
    isStutterWarning: false,
    tearlinePosPct: 0,
    flatnessScore: 99.99,
    jitterDistribution: [0, 0, 100, 0, 0],
  });

  const [shmState, setShmState] = useState<SharedMemoryState>(
    engineRef.current.getSharedMemoryState()
  );

  // Initialize engine defaults
  useEffect(() => {
    engineRef.current.setDisplayRefresh(144);
    engineRef.current.setMode(PacerMode.LatencyFirst);
    engineRef.current.setTargetFps(72.0);
    engineRef.current.setDelayBias(delayBias);
    engineRef.current.setProcess(selectedProcess.pid, selectedProcess.api);

    const shm = engineRef.current.getSharedMemoryState();
    setTargetFps(shm.targetFps);
    setShmState(shm);
    setTelemetry(engineRef.current.getTelemetry());

    const updateClock = () => {
      const d = new Date();
      setCurrentTimeStr(d.toLocaleTimeString([], { hour: 'numeric', minute: '2-digit' }));
      setCurrentDateStr(d.toLocaleDateString([], { month: 'numeric', day: 'numeric', year: 'numeric' }));
    };
    updateClock();
    const timer = setInterval(updateClock, 10000);
    return () => clearInterval(timer);
  }, []);

  const handleApplyFps = (fps: number) => {
    engineRef.current.setTargetFps(fps);
    const shm = engineRef.current.getSharedMemoryState();
    setTargetFps(shm.targetFps);
    setShmState(shm);
    setTelemetry(engineRef.current.getTelemetry());
  };

  const handleModeChange = (newMode: PacerMode) => {
    setMode(newMode);
    engineRef.current.setMode(newMode);
    const shm = engineRef.current.getSharedMemoryState();
    setTargetFps(shm.targetFps);
    setShmState(shm);
    setTelemetry(engineRef.current.getTelemetry());
  };

  const handleDelayBiasChange = (bias: number) => {
    setDelayBias(bias);
    engineRef.current.setDelayBias(bias);
    setShmState(engineRef.current.getSharedMemoryState());
    setTelemetry(engineRef.current.getTelemetry());
  };

  const handleDisplayRefreshChange = (hz: number) => {
    engineRef.current.setDisplayRefresh(hz);
    const shm = engineRef.current.getSharedMemoryState();
    setTargetFps(shm.targetFps);
    setShmState(shm);
    setTelemetry(engineRef.current.getTelemetry());
  };

  const handleClearAutoSnapNotification = () => {
    engineRef.current.clearAutoSnapNotification();
    setTelemetry(engineRef.current.getTelemetry());
  };

  const handleSelectProcess = (proc: ProcessTarget) => {
    setSelectedProcess(proc);
    engineRef.current.setProcess(proc.pid, proc.api);
    setShmState(engineRef.current.getSharedMemoryState());
  };

  const lastUiUpdateRef = useRef<number>(0);

  // Synchronous pacing tick called on each frame by the rendering benchmark
  const handlePaceFrame = (simulatedRenderTimeMs: number) => {
    const now = performance.now();
    engineRef.current.paceFrame(now, simulatedRenderTimeMs);
    
    // Throttle React UI re-renders to ~33ms (30 FPS) while engine records every frame at full speed
    if (now - lastUiUpdateRef.current >= 30) {
      lastUiUpdateRef.current = now;
      const newTelem = engineRef.current.getTelemetry();
      setTelemetry(newTelem);
      setShmState(engineRef.current.getSharedMemoryState());
    }
  };

  return (
    <div className="min-h-screen h-screen w-full bg-[#0a0b0e] text-[#e0e0e0] font-sans antialiased flex flex-col justify-between overflow-hidden select-none relative">
      {/* Subtle Windows 11 Dark Mica / Acrylic Desktop Backdrop */}
      <div className="absolute inset-0 bg-radial from-[#151722] via-[#0c0d12] to-[#08090c] opacity-90 pointer-events-none" />

      {/* Top Navigation Bar / Workspace Selector */}
      <div className="relative z-20 flex items-center justify-between px-4 py-2 border-b border-[#1f2129] bg-[#101115]/80 backdrop-blur-md">
        <div className="flex items-center space-x-3">
          <div className="w-5 h-5 rounded bg-cyan-500/20 border border-cyan-400/50 flex items-center justify-center text-[10px] text-cyan-400 font-bold font-mono">
            FP
          </div>
          <span className="text-xs font-semibold text-zinc-100 tracking-wide">
            Pacer — High-Precision Display-Locked Frame Scheduler
          </span>
          <span className="hidden sm:inline-block text-[10px] bg-emerald-950/70 border border-emerald-500/40 text-emerald-400 px-2 py-0.5 rounded font-mono">
            Console Smoothness Active (StdDev &lt; 0.001ms)
          </span>
        </div>

        {/* View Switcher Tabs */}
        <div className="flex items-center space-x-1 bg-[#161820] border border-[#252834] rounded p-0.5 text-xs font-medium">
          <button
            onClick={() => setActiveTab('workspace')}
            className={`flex items-center space-x-1.5 px-3 py-1 rounded transition-colors ${
              activeTab === 'workspace'
                ? 'bg-cyan-500 text-zinc-950 font-bold shadow-sm'
                : 'text-zinc-400 hover:text-zinc-200'
            }`}
          >
            <Layout className="w-3.5 h-3.5" />
            <span>Desktop Workspace</span>
          </button>
          <button
            onClick={() => setActiveTab('shm')}
            className={`flex items-center space-x-1.5 px-3 py-1 rounded transition-colors ${
              activeTab === 'shm'
                ? 'bg-cyan-500 text-zinc-950 font-bold shadow-sm'
                : 'text-zinc-400 hover:text-zinc-200'
            }`}
          >
            <Database className="w-3.5 h-3.5" />
            <span>Shared Memory</span>
          </button>
          <button
            onClick={() => setActiveTab('diagnostics')}
            className={`flex items-center space-x-1.5 px-3 py-1 rounded transition-colors ${
              activeTab === 'diagnostics'
                ? 'bg-cyan-500 text-zinc-950 font-bold shadow-sm'
                : 'text-zinc-400 hover:text-zinc-200'
            }`}
          >
            <FileCode className="w-3.5 h-3.5" />
            <span>C++ Core & Explainer</span>
          </button>
        </div>
      </div>

      {/* Main Workspace Area */}
      <div className="relative flex-1 p-3 md:p-5 overflow-y-auto flex items-center justify-center">
        {activeTab === 'workspace' && (
          <div className="max-w-6xl w-full grid grid-cols-1 lg:grid-cols-12 gap-5 items-start">
            {/* Left Column: Steam FramePacer Native Window */}
            <div className="lg:col-span-5 flex flex-col items-center sm:items-start space-y-4">
              <PacerNativeWindow
                targetFps={targetFps}
                onApplyFps={handleApplyFps}
                mode={mode}
                onModeChange={handleModeChange}
                delayBias={delayBias}
                onDelayBiasChange={handleDelayBiasChange}
                selectedProcess={selectedProcess}
                availableProcesses={AVAILABLE_PROCESSES}
                onSelectProcess={handleSelectProcess}
                liveFrametimeMs={telemetry.currentFrametimeMs}
                liveFps={telemetry.currentFps}
                onToggleGraph={() => setIsGraphOpen(!isGraphOpen)}
                isGraphOpen={isGraphOpen}
                ringData={shmState.ring}
                pllPhaseUs={telemetry.phaseSteeringUs}
                displayRefreshHz={telemetry.displayRefreshHz || 144}
                onDisplayRefreshChange={handleDisplayRefreshChange}
                divisorRatioLabel={telemetry.divisorRatioLabel}
                isDisplayDivisor={telemetry.isDisplayDivisor}
                isStutterWarning={telemetry.isStutterWarning}
                stutterReason={telemetry.stutterReason}
                renderHeadroomMs={telemetry.renderHeadroomMs}
                autoSnapNotification={telemetry.autoSnapNotification}
                onClearAutoSnapNotification={handleClearAutoSnapNotification}
              />
            </div>

            {/* Right Column: Oscilloscope & DirectX 11 Test Window */}
            <div className="lg:col-span-7 space-y-4">
              {/* Live DirectX 11 Render Process */}
              <LiveBenchmarkView
                targetFps={targetFps}
                telemetry={telemetry}
                mode={mode}
                onPaceFrame={handlePaceFrame}
              />

              {/* Frametime Oscilloscope Waveform */}
              {isGraphOpen && (
                <FrametimeOscilloscope
                  ringData={shmState.ring}
                  targetFps={targetFps}
                  telemetry={telemetry}
                  mode={mode}
                />
              )}
            </div>
          </div>
        )}

        {activeTab === 'shm' && (
          <div className="max-w-4xl w-full space-y-4">
            <SharedMemoryInspector shmState={shmState} />
            <FrametimeOscilloscope
              ringData={shmState.ring}
              targetFps={targetFps}
              telemetry={telemetry}
              mode={mode}
            />
          </div>
        )}

        {activeTab === 'diagnostics' && (
          <div className="max-w-5xl w-full space-y-4">
            <CodeFixExplainer />
            <RepoFileViewer />
          </div>
        )}
      </div>

      {/* Windows 11 Fluent Taskbar */}
      <div className="relative h-11 bg-[#101114]/95 backdrop-blur-md border-t border-[#22242c] flex items-center justify-between px-3 z-50 text-xs select-none">
        {/* Left / Center: Start Button, Search & Active App Icons */}
        <div className="flex items-center space-x-1.5 sm:space-x-2">
          {/* Windows Start Button */}
          <button
            id="win-start-btn"
            className="p-1.5 rounded hover:bg-white/10 active:bg-white/5 transition-colors flex items-center justify-center"
            title="Start"
          >
            <div className="w-4 h-4 grid grid-cols-2 gap-0.5">
              <div className="bg-cyan-400 rounded-[1px]" />
              <div className="bg-cyan-400 rounded-[1px]" />
              <div className="bg-cyan-400 rounded-[1px]" />
              <div className="bg-cyan-400 rounded-[1px]" />
            </div>
          </button>

          {/* Windows Search Bar */}
          <div className="hidden sm:flex items-center space-x-2 bg-[#1b1d24] border border-[#2b2d36] rounded-full px-3 py-1 text-zinc-400 text-xs w-44 hover:border-zinc-500 transition-colors cursor-text">
            <Search className="w-3.5 h-3.5 text-zinc-400" />
            <span className="text-[11px]">Search</span>
          </div>

          {/* Active Taskbar Item: Framepacer */}
          <div 
            onClick={() => setActiveTab('workspace')}
            className={`relative flex items-center space-x-1.5 px-2.5 py-1 rounded border cursor-pointer shadow-sm transition-colors ${
              activeTab === 'workspace' ? 'bg-white/10 border-white/10 text-zinc-100' : 'bg-transparent border-transparent text-zinc-400 hover:text-zinc-200'
            }`}
          >
            <div className="w-4 h-4 rounded bg-cyan-500/20 border border-cyan-400/50 flex items-center justify-center text-[10px] text-cyan-400 font-bold font-mono">
              FP
            </div>
            <span className="text-xs font-medium tracking-wide">Framepacer</span>
            {activeTab === 'workspace' && (
              <div className="absolute bottom-0 left-2 right-2 h-0.5 bg-cyan-400 rounded-full" />
            )}
          </div>

          {/* Active Taskbar Item: spinner.exe */}
          <div 
            onClick={() => setActiveTab('workspace')}
            className="relative hidden md:flex items-center space-x-1.5 px-2.5 py-1 hover:bg-white/5 rounded text-zinc-300 cursor-pointer transition-colors"
          >
            <div className="w-4 h-4 rounded bg-emerald-500/20 border border-emerald-400/50 flex items-center justify-center text-[9px] text-emerald-400 font-bold">
              🎮
            </div>
            <span className="text-xs font-medium">spinner.exe</span>
            <div className="absolute bottom-0 left-3 right-3 h-0.5 bg-emerald-400 rounded-full" />
          </div>
        </div>

        {/* Right: Windows System Tray & Clock */}
        <div className="flex items-center space-x-2 text-zinc-300">
          <button className="p-1 rounded hover:bg-white/10 transition-colors text-zinc-400">
            <ChevronUp className="w-3.5 h-3.5" />
          </button>

          {/* System Tray Icon: Framepacer (with live FPS tooltip) */}
          <div
            className="flex items-center space-x-1 px-1.5 py-0.5 rounded bg-cyan-950/60 border border-cyan-500/40 text-cyan-300 font-mono text-[10px] font-bold cursor-pointer"
            title={`Framepacer: ${telemetry.currentFps.toFixed(0)} FPS (${mode === PacerMode.LatencyFirst ? 'Latent Sync' : 'VBI Lock'})`}
          >
            <span>FP</span>
            <span className="w-1.5 h-1.5 rounded-full bg-emerald-400 animate-pulse" />
          </div>

          <div className="flex items-center space-x-2 px-1.5 py-1 text-zinc-400">
            <Wifi className="w-3.5 h-3.5" />
            <Volume2 className="w-3.5 h-3.5" />
          </div>

          {/* Date / Time */}
          <div className="text-right px-1.5 py-0.5 rounded hover:bg-white/10 cursor-pointer font-sans leading-tight">
            <div className="text-[11px] text-zinc-200 font-medium">{currentTimeStr}</div>
            <div className="text-[10px] text-zinc-400">{currentDateStr}</div>
          </div>
        </div>
      </div>
    </div>
  );
}

