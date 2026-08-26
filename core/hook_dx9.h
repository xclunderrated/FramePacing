#pragma once

namespace pacer {

// Hooks IDirect3DDevice9::Present/Reset (vtable idx 17/16) for the whole
// process via the dummy-device trick. No DXGI display clock (D3D9 has none) ->
// engine runs free on the nominal/user period.
bool hook_dx9_install();

}  // namespace pacer