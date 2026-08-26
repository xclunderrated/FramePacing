// Header-only consumer side of the shm spec (service, UI, tools).
#pragma once

#include "shm.h"

namespace pacer {

struct ShmBox {
    HANDLE map = nullptr;
    SharedMemLayout* shm = nullptr;
    LONG last_idx = 0;
    bool seen = false;

    bool open(std::uint32_t pid, bool write = false) {
        if (map) return true;
        map = OpenFileMappingW(write ? FILE_MAP_ALL_ACCESS : FILE_MAP_READ, FALSE,
                               shm_name(pid).c_str());
        if (!map) return false;
        shm = static_cast<SharedMemLayout*>(
            MapViewOfFile(map, write ? FILE_MAP_ALL_ACCESS : FILE_MAP_READ, 0, 0,
                          sizeof(SharedMemLayout)));
        if (!shm || shm->ctl.magic != kShmMagic) {  // core not ready yet
            close();
            return false;
        }
        return true;
    }
    void close() {
        if (shm) UnmapViewOfFile(shm);
        if (map) CloseHandle(map);
        shm = nullptr;
        map = nullptr;
        seen = false;
    }
    ~ShmBox() { close(); }

    bool valid() const { return shm != nullptr; }
    LONG idx() const { return shm ? shm->write_idx : 0; }
};

}  // namespace pacer
