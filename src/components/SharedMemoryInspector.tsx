import React from 'react';
import { SharedMemoryState, PacerState, PacerMode, PacerApi } from '../types';
import { Database, Cpu, Layers, HardDrive, Hash, Radio } from 'lucide-react';

interface Props {
  shmState: SharedMemoryState;
}

export const SharedMemoryInspector: React.FC<Props> = ({ shmState }) => {
  const getPacerStateName = (s: PacerState) => (s === PacerState.Limited ? 'Limited (1)' : 'Unlimited (0)');
  const getPacerModeName = (m: PacerMode) => {
    switch (m) {
      case PacerMode.LatencyFirst:
        return 'LatencyFirst (3)';
      case PacerMode.DisplayLocked:
        return 'DisplayLocked (0)';
      case PacerMode.VrrLive:
        return 'VrrLive (2)';
      case PacerMode.Async:
        return 'Async (1)';
      default:
        return 'Unknown';
    }
  };

  const getPacerApiName = (a: PacerApi) => {
    switch (a) {
      case PacerApi.Dxgi:
        return 'Dxgi (1)';
      case PacerApi.D3D9:
        return 'D3D9 (4)';
      case PacerApi.Vulkan:
        return 'Vulkan (2)';
      case PacerApi.OpenGL:
        return 'OpenGL (3)';
      case PacerApi.DDraw:
        return 'DDraw (5)';
      default:
        return 'Unknown (0)';
    }
  };

  return (
    <div className="w-full bg-[#141518] rounded-lg border border-[#272930] p-3.5 space-y-3 font-sans">
      <div className="flex items-center justify-between">
        <div className="flex items-center space-x-2">
          <Database className="w-4 h-4 text-cyan-400" />
          <span className="text-xs font-semibold text-zinc-200 uppercase tracking-wider">
            Shared Memory Layout (<code className="font-mono text-cyan-300">Local\Pacer.SHM.{shmState.pid}</code>)
          </span>
        </div>
        <div className="flex items-center space-x-1.5 text-[11px] font-mono text-zinc-400">
          <span className="w-2 h-2 rounded-full bg-cyan-400" />
          <span>4096 Ring Slots</span>
        </div>
      </div>

      {/* Control Block Fields Table */}
      <div className="grid grid-cols-2 sm:grid-cols-3 gap-2 text-xs font-mono">
        <div className="bg-[#191b20] border border-[#262830] rounded p-2 flex flex-col">
          <span className="text-[10px] text-zinc-500">magic / version</span>
          <span className="text-zinc-200 font-bold mt-0.5">
            0x{shmState.magic.toString(16).toUpperCase()} ('PACR') v{shmState.version}
          </span>
        </div>

        <div className="bg-[#191b20] border border-[#262830] rounded p-2 flex flex-col">
          <span className="text-[10px] text-zinc-500">ctl.state (Interlocked)</span>
          <span className={`font-bold mt-0.5 ${shmState.state === PacerState.Limited ? 'text-emerald-400' : 'text-zinc-400'}`}>
            {getPacerStateName(shmState.state)}
          </span>
        </div>

        <div className="bg-[#191b20] border border-[#262830] rounded p-2 flex flex-col">
          <span className="text-[10px] text-zinc-500">ctl.mode</span>
          <span className="text-cyan-300 font-bold mt-0.5">
            {getPacerModeName(shmState.mode)}
          </span>
        </div>

        <div className="bg-[#191b20] border border-[#262830] rounded p-2 flex flex-col">
          <span className="text-[10px] text-zinc-500">ctl.target_fps_bits</span>
          <span className="text-amber-400 font-bold mt-0.5">
            {shmState.targetFps > 0 ? `${shmState.targetFps.toFixed(3)} FPS` : '0.000 (Uncapped)'}
          </span>
        </div>

        <div className="bg-[#191b20] border border-[#262830] rounded p-2 flex flex-col">
          <span className="text-[10px] text-zinc-500">ctl.delay_bias_bits</span>
          <span className="text-zinc-200 font-bold mt-0.5">
            {(shmState.delayBias * 100).toFixed(0)}% ({(shmState.delayBias).toFixed(2)})
          </span>
        </div>

        <div className="bg-[#191b20] border border-[#262830] rounded p-2 flex flex-col">
          <span className="text-[10px] text-zinc-500">ctl.api</span>
          <span className="text-zinc-200 font-bold mt-0.5">
            {getPacerApiName(shmState.api)}
          </span>
        </div>

        <div className="bg-[#191b20] border border-[#262830] rounded p-2 flex flex-col">
          <span className="text-[10px] text-zinc-500">write_idx (alignas 64)</span>
          <span className="text-emerald-400 font-bold mt-0.5">
            {shmState.writeIdx.toLocaleString()}
          </span>
        </div>

        <div className="bg-[#191b20] border border-[#262830] rounded p-2 flex flex-col">
          <span className="text-[10px] text-zinc-500">qpc_frequency</span>
          <span className="text-zinc-300 font-bold mt-0.5">
            {(shmState.qpcFrequency / 1e6).toFixed(1)} MHz
          </span>
        </div>

        <div className="bg-[#191b20] border border-[#262830] rounded p-2 flex flex-col">
          <span className="text-[10px] text-zinc-500">measured_refresh_hz</span>
          <span className="text-cyan-400 font-bold mt-0.5">
            {shmState.measuredRefreshHz.toFixed(2)} Hz
          </span>
        </div>
      </div>
    </div>
  );
};
