// Vulkan + OpenGL export hooks. Arm immediately if the runtime is already
// mapped, otherwise lazily when a DLL-load notification reports the module.
#pragma once

namespace pacer {

void hook_exports_arm_loaded();    // vulkan-1/opengl32 if already present
bool hook_exports_arm_vulkan();    // idempotent
bool hook_exports_arm_opengl();    // idempotent

}  // namespace pacer
