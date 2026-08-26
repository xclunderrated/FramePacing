import { PacerMode, PacerState, PacerApi, SharedMemoryState, PacerTelemetry } from '../types';

export class SimulatedPacerEngine {
  private freq: number = 10_000_000; // 10 MHz QPC timer
  private targetFps: number = 60.0;
  private mode: PacerMode = PacerMode.LatencyFirst;
  private state: PacerState = PacerState.Limited;
  private delayBias: number = 0.0;
  private api: PacerApi = PacerApi.Dxgi;
  private pid: number = 14280;

  private periodTicks: number = 0;
  private nextTargetTicks: number = 0;
  private initialized: boolean = false;
  private pllPhaseUs: number = 0;
  private isDisplayDivisor: boolean = false;
  private divisorRatioLabel: string = '';
  private displayRefreshHz: number = 144.0;
  private lastVbiTicks: number = 0;

  // Adaptive render headroom tracking
  private avgRenderMs: number = 3.0;
  private peakRenderMs: number = 3.5;
  private renderHeadroomMs: number = 13.6;
  private isStutterWarning: boolean = false;
  private stutterReason: string = '';
  private tearlinePosPct: number = 0; // 0% = top bezel (parked), >0% = rolling tear
  private autoSnapNotification?: {
    originalFps: number;
    snappedFps: number;
    ratioLabel: string;
    refreshHz: number;
    reason: string;
    timestamp: number;
  };

  // Shared Memory State
  private ringCapacity: number = 256;
  private ring: number[] = new Array(256).fill(16.666);
  private writeIdx: number = 0;

  private frameCount: number = 0;

  constructor() {
    this.recalculatePeriod();
  }

  /**
   * Calculates the nearest clean monitor divisor or cadence FPS for Latent Sync
   */
  public getNearestDivisor(requestedFps: number, refreshHz: number = this.displayRefreshHz): {
    fps: number;
    ratioLabel: string;
  } {
    if (requestedFps <= 0) return { fps: 0, ratioLabel: 'Uncapped' };

    const candidates: Array<{ fps: number; ratioLabel: string }> = [
      { fps: refreshHz, ratioLabel: `1:1 Native (${refreshHz} Hz)` },
      { fps: refreshHz / 2, ratioLabel: `1:2 Divisor (${(refreshHz / 2).toFixed(1)} FPS)` },
      { fps: (refreshHz * 2) / 3, ratioLabel: `2:3 Cadence (${((refreshHz * 2) / 3).toFixed(1)} FPS)` },
      { fps: (refreshHz * 3) / 4, ratioLabel: `3:4 Cadence (${((refreshHz * 3) / 4).toFixed(1)} FPS)` },
      { fps: refreshHz / 3, ratioLabel: `1:3 Divisor (${(refreshHz / 3).toFixed(1)} FPS)` },
      { fps: refreshHz / 4, ratioLabel: `1:4 Divisor (${(refreshHz / 4).toFixed(1)} FPS)` },
    ];

    let best = candidates[0];
    let minDiff = Math.abs(requestedFps - best.fps);

    for (let i = 1; i < candidates.length; i++) {
      const diff = Math.abs(requestedFps - candidates[i].fps);
      if (diff < minDiff) {
        minDiff = diff;
        best = candidates[i];
      }
    }

    return best;
  }

  public setTargetFps(fps: number, autoSnap: boolean = true) {
    let finalFps = Math.max(0, fps);

    // If Latent Sync is active, automatically snap non-divisors to the nearest clean cadence
    if (autoSnap && this.mode === PacerMode.LatencyFirst && finalFps > 0) {
      const nearest = this.getNearestDivisor(finalFps, this.displayRefreshHz);
      const isAlreadyDivisor = Math.abs(nearest.fps - finalFps) <= 0.05;

      if (!isAlreadyDivisor) {
        const original = finalFps;
        finalFps = Math.round(nearest.fps * 100) / 100;
        this.autoSnapNotification = {
          originalFps: original,
          snappedFps: finalFps,
          ratioLabel: nearest.ratioLabel,
          refreshHz: this.displayRefreshHz,
          reason: `Latent Sync auto-snapped ${original} → ${finalFps} FPS (${nearest.ratioLabel}) to park the tearline off-screen and eliminate harmonic beat waves on your ${Math.round(this.displayRefreshHz)} Hz display.`,
          timestamp: Date.now(),
        };
      }
    }

    this.targetFps = finalFps;
    this.state = finalFps > 0.0 ? PacerState.Limited : PacerState.Unlimited;
    this.recalculatePeriod();
    this.resetRingBuffer();
    this.initialized = false;
  }

  public setMode(mode: PacerMode) {
    this.mode = mode;

    // When switching into Latent Sync, auto-snap current target FPS if it isn't an exact divisor
    if (mode === PacerMode.LatencyFirst && this.targetFps > 0) {
      const nearest = this.getNearestDivisor(this.targetFps, this.displayRefreshHz);
      if (Math.abs(nearest.fps - this.targetFps) > 0.05) {
        const original = this.targetFps;
        this.targetFps = Math.round(nearest.fps * 100) / 100;
        this.autoSnapNotification = {
          originalFps: original,
          snappedFps: this.targetFps,
          ratioLabel: nearest.ratioLabel,
          refreshHz: this.displayRefreshHz,
          reason: `Latent Sync activated: auto-snapped target to ${this.targetFps} FPS (${nearest.ratioLabel}) for tear-free alignment on your ${Math.round(this.displayRefreshHz)} Hz display.`,
          timestamp: Date.now(),
        };
      }
    }

    this.recalculatePeriod();
    this.resetRingBuffer();
    this.initialized = false;
  }

  private resetRingBuffer() {
    const defaultMs = this.targetFps > 0 ? 1000.0 / this.targetFps : 3.0;
    this.ring.fill(defaultMs);
  }

  public clearAutoSnapNotification() {
    this.autoSnapNotification = undefined;
  }

  public setDelayBias(bias: number) {
    this.delayBias = Math.max(0.0, Math.min(1.0, bias));
  }

  public setDisplayRefresh(hz: number) {
    this.displayRefreshHz = hz;

    // If in Latent Sync, auto-retune to nearest divisor of new refresh rate
    if (this.mode === PacerMode.LatencyFirst && this.targetFps > 0) {
      const nearest = this.getNearestDivisor(this.targetFps, hz);
      if (Math.abs(nearest.fps - this.targetFps) > 0.05) {
        const original = this.targetFps;
        this.targetFps = Math.round(nearest.fps * 100) / 100;
        this.autoSnapNotification = {
          originalFps: original,
          snappedFps: this.targetFps,
          ratioLabel: nearest.ratioLabel,
          refreshHz: hz,
          reason: `Monitor refresh changed to ${hz} Hz: auto-realigned Latent Sync to ${this.targetFps} FPS (${nearest.ratioLabel}) to preserve off-screen tearline parking.`,
          timestamp: Date.now(),
        };
      }
    }

    this.recalculatePeriod();
    this.initialized = false;
  }

  public getDisplayRefresh(): number {
    return this.displayRefreshHz;
  }

  public setProcess(pid: number, api: PacerApi) {
    this.pid = pid;
    this.api = api;
  }

  private recalculatePeriod() {
    if (this.targetFps <= 0) {
      this.periodTicks = 0;
      this.isDisplayDivisor = false;
      this.divisorRatioLabel = 'Uncapped / Freerun';
      this.isStutterWarning = false;
      this.stutterReason = '';
      return;
    }

    const userPeriod = this.freq / this.targetFps;
    const dispPeriod = this.freq / this.displayRefreshHz;

    // Check display divider snapping (integer divisors & fractional cadences)
    if (
      (this.mode === PacerMode.DisplayLocked || this.mode === PacerMode.LatencyFirst) &&
      this.targetFps > 1.0
    ) {
      // 1. Integer divisors: 1:1, 1:2, 1:3, 1:4 (e.g. 144, 72, 48 on 144Hz; 120, 60, 40, 30 on 120Hz)
      const k = Math.round(this.displayRefreshHz / this.targetFps);
      if (k >= 1 && k <= 16) {
        const snappedHz = this.displayRefreshHz / k;
        const errPct = Math.abs(snappedHz - this.targetFps) / this.targetFps;
        if (errPct <= 0.008) {
          this.isDisplayDivisor = true;
          this.divisorRatioLabel = `1/${k} Cadence (${this.displayRefreshHz} Hz locked)`;
          this.periodTicks = dispPeriod * k;
          this.isStutterWarning = false;
          this.stutterReason = '';
          return;
        }
      }

      // 2. Fractional cadences: 2:3, 3:2, 3:4, 4:3
      const fractions = [
        { num: 2, den: 3, label: '2:3 Pulldown' },
        { num: 3, den: 2, label: '3:2 Pulldown' },
        { num: 3, den: 4, label: '3:4 Pulldown' },
        { num: 4, den: 3, label: '4:3 Pulldown' },
      ];
      for (const frac of fractions) {
        const candidateHz = (this.displayRefreshHz * frac.num) / frac.den;
        const errPct = Math.abs(candidateHz - this.targetFps) / this.targetFps;
        if (errPct <= 0.005) {
          this.isDisplayDivisor = true;
          this.divisorRatioLabel = `${frac.label} (${this.displayRefreshHz} Hz)`;
          this.periodTicks = (dispPeriod * frac.den) / frac.num;
          this.isStutterWarning = false;
          this.stutterReason = '';
          return;
        }
      }
    }

    this.isDisplayDivisor = false;
    this.periodTicks = userPeriod;

    if (this.mode === PacerMode.LatencyFirst) {
      this.divisorRatioLabel = `Non-Divisor (${this.targetFps.toFixed(0)} FPS vs ${this.displayRefreshHz} Hz)`;
      this.isStutterWarning = true;
      this.stutterReason = `Latent Sync requires target FPS to match or divide monitor refresh (${this.displayRefreshHz} Hz: use ${Math.round(this.displayRefreshHz)} or ${Math.round(this.displayRefreshHz/2)} FPS). Non-divisors cause tearline drift and 3:2 frame judder.`;
    } else if (this.mode === PacerMode.VrrLive) {
      this.divisorRatioLabel = `VRR Active (G-Sync/FreeSync)`;
      this.isStutterWarning = false;
      this.stutterReason = '';
    } else {
      this.divisorRatioLabel = `Async Independent`;
      this.isStutterWarning = false;
      this.stutterReason = '';
    }
  }

  /**
   * Simulates the exact pace_frame call executed before present with adaptive headroom
   */
  public paceFrame(nowMs: number, simulatedRenderTimeMs: number): number {
    const nowTicks = (nowMs / 1000) * this.freq;
    const dispPeriod = this.freq / this.displayRefreshHz;

    // Track smoothed render workload
    this.avgRenderMs = 0.88 * this.avgRenderMs + 0.12 * simulatedRenderTimeMs;
    this.peakRenderMs = Math.max(this.avgRenderMs * 1.15, simulatedRenderTimeMs);

    // Target frame interval in ms
    const targetIntervalMs = this.periodTicks > 0 ? (this.periodTicks / this.freq) * 1000 : 16.666;
    this.renderHeadroomMs = Math.max(0, targetIntervalMs - this.peakRenderMs);

    // Update VBI phase
    this.lastVbiTicks = Math.floor(nowTicks / dispPeriod) * dispPeriod;

    if (this.state === PacerState.Unlimited || this.targetFps <= 1.0 || this.periodTicks <= 0) {
      // Uncapped: frame renders at native GPU/CPU workload speed
      const frameDeltaMs = Math.max(0.5, simulatedRenderTimeMs + (Math.random() - 0.5) * 0.04);
      this.recordFrame(frameDeltaMs);
      this.tearlinePosPct = (this.frameCount * 7) % 100;
      return 0;
    }

    // Deterministic Rigid Sequence Scheduling
    if (!this.initialized || nowTicks > this.nextTargetTicks + 1.5 * this.periodTicks) {
      this.nextTargetTicks = nowTicks + this.periodTicks;
      this.initialized = true;
    } else {
      this.nextTargetTicks += this.periodTicks;
    }

    // Adaptive Latent Sync Headroom Guarding:
    // Clamp back-edge delay so that render workload + safety margin NEVER starves frame start
    let backBudgetTicks = 0;
    if (this.mode === PacerMode.LatencyFirst) {
      const requestedBackTicks = this.delayBias * this.periodTicks;
      const safetyMarginTicks = 0.0015 * this.freq; // 1.5ms safety cushion
      const peakRenderTicks = (this.peakRenderMs / 1000) * this.freq;
      const maxSafeBackTicks = Math.max(0, this.periodTicks - peakRenderTicks - safetyMarginTicks);
      backBudgetTicks = Math.min(requestedBackTicks, maxSafeBackTicks);

      if (this.isDisplayDivisor) {
        this.tearlinePosPct = 0; // Perfectly parked in top bezel
      } else {
        // Rolling tearline position across screen when non-divisor is used
        const cycle = (this.displayRefreshHz / this.targetFps);
        this.tearlinePosPct = Math.round(((this.frameCount * (cycle - Math.floor(cycle))) % 1.0) * 100);
      }
    } else {
      this.tearlinePosPct = 0;
    }

    // Phase-lock to measured VBlank for DisplayLocked and LatentSync with critically-damped deadband
    if (
      (this.mode === PacerMode.DisplayLocked || this.mode === PacerMode.LatencyFirst) &&
      this.isDisplayDivisor
    ) {
      const p = dispPeriod;
      const vbi = this.lastVbiTicks;
      const targetPhase = this.mode === PacerMode.DisplayLocked ? -0.25 * p : 0.0;
      const k = Math.round((this.nextTargetTicks - vbi) / p);
      const expectedVbi = vbi + k * p;
      let err = this.nextTargetTicks - expectedVbi - targetPhase;
      err = ((err + 0.5 * p) % p) - 0.5 * p;

      const errUs = (err * 1e6) / this.freq;
      this.pllPhaseUs = errUs;

      // 40 µs deadband eliminates resonant hunting / standing waves
      if (Math.abs(errUs) > 40) {
        const slewLimit = 0.0000002 * this.freq;
        let steer = 0.0004 * err;
        if (steer > slewLimit) steer = slewLimit;
        if (steer < -slewLimit) steer = -slewLimit;
        this.nextTargetTicks -= steer;
      }
    } else {
      this.pllPhaseUs = 0.0;
    }

    // Actual frame delivery calculation: absolute flatline precision (< 0.0001 ms jitter)
    let deliveredFrametimeMs = targetIntervalMs;

    // Check if frame spiked beyond interval -> real stutter hitch (e.g. texture load spike)
    if (simulatedRenderTimeMs > targetIntervalMs) {
      deliveredFrametimeMs = targetIntervalMs + (simulatedRenderTimeMs - targetIntervalMs);
    } else {
      // Clean locked cadence: flatline (standard deviation < 0.0001 ms, zero waves)
      const simulatedJitter = (Math.random() - 0.5) * 0.0001;
      deliveredFrametimeMs = targetIntervalMs + simulatedJitter;
    }

    this.recordFrame(deliveredFrametimeMs);
    return (backBudgetTicks / this.freq) * 1000;
  }

  private recordFrame(deltaMs: number) {
    this.ring[this.writeIdx % this.ringCapacity] = deltaMs;
    this.writeIdx++;
    this.frameCount++;
  }

  public getSharedMemoryState(): SharedMemoryState {
    return {
      magic: 0x50414352, // 'PACR'
      version: 1,
      state: this.state,
      mode: this.mode,
      api: this.api,
      pid: this.pid,
      qpcFrequency: this.freq,
      targetFps: this.targetFps,
      delayBias: this.delayBias,
      measuredRefreshHz: this.displayRefreshHz,
      lastVbiQpc: this.lastVbiTicks,
      pllPhaseUs: this.pllPhaseUs,
      exitRequested: 0,
      writeIdx: this.writeIdx,
      ring: [...this.ring],
    };
  }

  public getTelemetry(): PacerTelemetry {
    const recent = this.ring.slice(-80);
    const currentFrametimeMs = this.ring[(this.writeIdx - 1) % this.ringCapacity] || 16.666;
    const currentFps = currentFrametimeMs > 0 ? 1000.0 / currentFrametimeMs : 0;
    const targetIntervalMs = this.periodTicks > 0 ? (this.periodTicks / this.freq) * 1000 : 16.666;

    const sum = recent.reduce((a, b) => a + b, 0);
    const mean = sum / (recent.length || 1);
    const variance = recent.reduce((a, b) => a + Math.pow(b - mean, 2), 0) / (recent.length || 1);
    const stdDev = Math.sqrt(variance);
    const stdDevUs = stdDev * 1000;

    const sorted = [...recent].sort((a, b) => a - b);
    const minMs = sorted[0] || currentFrametimeMs;
    const maxMs = sorted[sorted.length - 1] || currentFrametimeMs;
    const p99Index = Math.min(sorted.length - 1, Math.floor(sorted.length * 0.99));
    const p99_9Index = Math.min(sorted.length - 1, Math.floor(sorted.length * 0.999));
    const p99Ms = sorted[p99Index] || maxMs;
    const p99_9Ms = sorted[p99_9Index] || maxMs;

    // Calculate Flatness Score: percentage of frames delivered within ±0.02ms of target
    let inWindowFrames = 0;
    const jitterBuckets = [0, 0, 0, 0, 0]; // [<-0.05ms, -0.05..-0.01ms, -0.01..+0.01ms (dead center), +0.01..+0.05ms, >+0.05ms]

    recent.forEach((ft) => {
      const delta = ft - targetIntervalMs;
      if (Math.abs(delta) <= 0.02) {
        inWindowFrames++;
      }

      if (delta < -0.05) jitterBuckets[0]++;
      else if (delta < -0.01) jitterBuckets[1]++;
      else if (delta <= 0.01) jitterBuckets[2]++;
      else if (delta <= 0.05) jitterBuckets[3]++;
      else jitterBuckets[4]++;
    });

    const flatnessScore = this.state === PacerState.Limited && this.targetFps > 0
      ? Math.min(100, Math.max(0, (inWindowFrames / recent.length) * 100))
      : 100;

    return {
      currentFps,
      currentFrametimeMs,
      meanFrametimeMs: mean,
      stdDevMs: stdDev,
      stdDevUs,
      minMs,
      maxMs,
      p99Ms,
      p99_9Ms,
      totalFrames: this.frameCount,
      isDisplayDivisor: this.isDisplayDivisor,
      phaseSteeringUs: this.pllPhaseUs,
      isTearingAllowed: this.mode === PacerMode.LatencyFirst && this.state === PacerState.Limited,
      renderHeadroomMs: this.renderHeadroomMs,
      displayRefreshHz: this.displayRefreshHz,
      divisorRatioLabel: this.divisorRatioLabel,
      isStutterWarning: this.isStutterWarning,
      stutterReason: this.stutterReason,
      tearlinePosPct: this.tearlinePosPct,
      flatnessScore,
      jitterDistribution: jitterBuckets.map(b => (b / recent.length) * 100),
      autoSnapNotification: this.autoSnapNotification,
    };
  }
}
