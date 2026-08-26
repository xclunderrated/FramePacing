// Lazy hook arming: games may load vulkan-1.dll / opengl32.dll only after
// we are already injected. LdrRegisterDllNotification (ntdll) fires on module
// load; we park the request and arm hooks from the worker thread, outside
// loader lock.
#pragma once

namespace pacer {

bool loader_watch_install();
void loader_watch_poll();  // call ~4 Hz from the worker thread

}  // namespace pacer
