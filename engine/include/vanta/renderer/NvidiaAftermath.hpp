#pragma once

#include <Windows.h>
#include <d3d12.h>

#include <string>

#if defined(VANTA_ENABLE_AFTERMATH)
#include <GFSDK_Aftermath.h>
#include <GFSDK_Aftermath_GpuCrashDump.h>
#endif

namespace vanta::renderer
{
struct NvidiaAftermathStatus
{
    bool sdkCompiledIn = false;
    bool crashDumpsEnabled = false;
    bool deviceInitialized = false;
    std::string diagnostic;
};

// Optional NVIDIA Nsight Aftermath integration. Crash dump collection is
// enabled before D3D12 device creation, as required by the SDK.
class NvidiaAftermathRuntime final
{
public:
    NvidiaAftermathRuntime() = default;
    ~NvidiaAftermathRuntime();

    NvidiaAftermathRuntime(const NvidiaAftermathRuntime&) = delete;
    NvidiaAftermathRuntime& operator=(const NvidiaAftermathRuntime&) = delete;

    [[nodiscard]] bool InitializeBeforeDevice(std::string& error) noexcept;
    [[nodiscard]] bool InitializeDevice(ID3D12Device* device, std::string& error) noexcept;
    void Shutdown() noexcept;

    [[nodiscard]] const NvidiaAftermathStatus& Status() const noexcept { return m_status; }

private:
#if defined(VANTA_ENABLE_AFTERMATH)
    static void GpuCrashDumpCallback(const void* dump, std::uint32_t size, void* userData) noexcept;
    static void CrashDumpDescriptionCallback(
        PFN_GFSDK_Aftermath_AddGpuCrashDumpDescription addDescription, void* userData) noexcept;
    void WriteCrashDump(const void* dump, std::uint32_t size) noexcept;
#endif

    NvidiaAftermathStatus m_status;
};
} // namespace vanta::renderer
