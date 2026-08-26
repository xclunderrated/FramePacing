export enum PacerMode {
  DisplayLocked = 0, // Console front-edge: VBI phase-locked loop on measured refresh
  Async = 1,         // RTSS-style post-present wait (compat fallback)
  VrrLive = 2,       // VRR Live adaptive sync (G-Sync / FreeSync)
  LatencyFirst = 3,  // Special K Latent sync (tearline parking + adaptive delay bias)
}

export enum PacerState {
  Unlimited = 0,
  Limited = 1,
}

export enum PacerApi {
  Unknown = 0,
  Dxgi = 1,
  Vulkan = 2,
  OpenGL = 3,
  D3D9 = 4,
  DDraw = 5,
}

export interface SharedMemoryState {
  magic: number; // 0x50414352 ('PACR')
  version: number;
  state: PacerState;
  mode: PacerMode;
  api: PacerApi;
  pid: number;
  qpcFrequency: number;
  targetFps: number;
  delayBias: number;
  measuredRefreshHz: number;
  lastVbiQpc: number;
  pllPhaseUs: number;
  exitRequested: number;
  writeIdx: number;
  ring: number[]; // Frame delta ms
}

export interface PacerTelemetry {
  currentFps: number;
  currentFrametimeMs: number;
  meanFrametimeMs: number;
  stdDevMs: number;
  stdDevUs: number;
  minMs: number;
  maxMs: number;
  p99Ms: number;
  p99_9Ms: number;
  totalFrames: number;
  isDisplayDivisor: boolean;
  phaseSteeringUs: number;
  isTearingAllowed: boolean;
  renderHeadroomMs: number;
  displayRefreshHz: number;
  divisorRatioLabel?: string;
  isStutterWarning: boolean;
  stutterReason?: string;
  tearlinePosPct: number;
  flatnessScore: number; // 0 to 100% percentage of frames within ±0.02ms of target
  jitterDistribution: number[]; // 5-bucket histogram of jitter (<-0.05ms, -0.05..-0.01ms, -0.01..+0.01ms, +0.01..+0.05ms, >+0.05ms)
  autoSnapNotification?: {
    originalFps: number;
    snappedFps: number;
    ratioLabel: string;
    refreshHz: number;
    reason: string;
    timestamp: number;
  };
}

export interface ProcessTarget {
  pid: number;
  name: string;
  api: PacerApi;
  is64Bit: boolean;
  active: boolean;
  nativeFps: number;
  engine?: string;
  windowTitle?: string;
  isRunning?: boolean;
}

