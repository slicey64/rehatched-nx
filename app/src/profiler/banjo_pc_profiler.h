#pragma once

// PC sampling profiler (cvar banjo_pc_profile_s); see banjo_pc_profiler.cpp. Does nothing on the Switch, where the
// SDK's sampler (sdk/src/ui/switch_perf.cpp) does the same job.
namespace banjo::pc_profiler {

#if defined(_WIN32)
void Start();
void Stop();
#else
inline void Start() {}
inline void Stop() {}
#endif

}  // namespace banjo::pc_profiler
