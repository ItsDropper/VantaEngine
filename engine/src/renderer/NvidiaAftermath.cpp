#include "vanta/renderer/NvidiaAftermath.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace vanta::renderer
{
namespace
{
std::filesystem::path CrashDumpDirectory()
{
    std::vector<wchar_t> localAppData(32768U, L'\\0');
    const DWORD length = GetEnvironmentVariableW(
        L"LOCALAPPDATA", localAppData.data(), static_cast<DWORD>(localAppData.size()));
    if (length > 0U && length < localAppData.size())
        return std::filesystem::path(localAppData.data()) / L"VantaEngine" / L"CrashDumps";
    return std::filesystem::current_path() / L"VantaEngine" / L"CrashDumps";
}
}

NvidiaAftermathRuntime::~NvidiaAftermathRuntime()
{
    Shutdown();
}

bool NvidiaAftermathRuntime::InitializeBeforeDevice(std::string& error)
{
#if defined(VANTA_ENABLE_AFTERMATH)
    m_status.sdkCompiledIn = true;
    const GFSDK_Aftermath_Result result = GFSDK_Aftermath_EnableGpuCrashDumps(
        GFSDK_Aftermath_Version_API,
        GFSDK_Aftermath_GpuCrashDumpWatchedApiFlags_DX,
        GFSDK_Aftermath_GpuCrashDumpFeatureFlags_DeferDebugInfoCallbacks,
        &NvidiaAftermathRuntime::GpuCrashDumpCallback,
        nullptr,
        &NvidiaAftermathRuntime::CrashDumpDescriptionCallback,
        nullptr,
        this);
    if (!GFSDK_Aftermath_SUCCEED(result)) {
        error = "Nsight Aftermath crash-dump registration failed (result " +
            std::to_string(static_cast<std::uint32_t>(result)) + ").";
        m_status.diagnostic = error;
        return false;
    }
    m_status.crashDumpsEnabled = true;
    m_status.diagnostic = "Nsight Aftermath GPU crash-dump collection enabled.";
    error.clear();
    return true;
#else
    error = "Nsight Aftermath SDK is not configured in this build.";
    m_status.diagnostic = error;
    return false;
#endif
}

bool NvidiaAftermathRuntime::InitializeDevice(
    ID3D12Device* device, std::string& error)
{
#if defined(VANTA_ENABLE_AFTERMATH)
    if (!m_status.crashDumpsEnabled) {
        error = "Nsight Aftermath must register crash-dump callbacks before D3D12 device creation.";
        return false;
    }
    if (device == nullptr) {
        error = "Cannot initialize Nsight Aftermath with a null D3D12 device.";
        return false;
    }
    constexpr std::uint32_t featureFlags =
        GFSDK_Aftermath_FeatureFlags_EnableMarkers |
        GFSDK_Aftermath_FeatureFlags_EnableResourceTracking;
    const GFSDK_Aftermath_Result result = GFSDK_Aftermath_DX12_Initialize(
        GFSDK_Aftermath_Version_API, featureFlags, device);
    if (!GFSDK_Aftermath_SUCCEED(result)) {
        error = "Nsight Aftermath D3D12 initialization failed (result " +
            std::to_string(static_cast<std::uint32_t>(result)) + ").";
        m_status.diagnostic = error;
        return false;
    }
    m_status.deviceInitialized = true;
    m_status.diagnostic =
        "Nsight Aftermath active: D3D12 GPU crash dumps, event markers and resource tracking enabled.";
    error.clear();
    return true;
#else
    (void)device;
    error = "Nsight Aftermath SDK is not configured in this build.";
    return false;
#endif
}

#if defined(VANTA_ENABLE_AFTERMATH)
void NvidiaAftermathRuntime::GpuCrashDumpCallback(
    const void* dump, const std::uint32_t size, void* userData) noexcept
{
    if (userData == nullptr || dump == nullptr || size == 0U)
        return;
    static_cast<NvidiaAftermathRuntime*>(userData)->WriteCrashDump(dump, size);
}

void NvidiaAftermathRuntime::CrashDumpDescriptionCallback(
    PFN_GFSDK_Aftermath_AddGpuCrashDumpDescription addDescription, void* userData) noexcept
{
    if (addDescription == nullptr || userData == nullptr)
        return;
    addDescription(GFSDK_Aftermath_GpuCrashDumpDescriptionKey_ApplicationName, "VantaEngine");
    addDescription(GFSDK_Aftermath_GpuCrashDumpDescriptionKey_ApplicationVersion, "0.2.0");
    addDescription(GFSDK_Aftermath_GpuCrashDumpDescriptionKey_UserDefined,
        "D3D12 renderer; inspect the accompanying application log for frame and pass context.");
}

void NvidiaAftermathRuntime::WriteCrashDump(
    const void* dump, const std::uint32_t size) noexcept
{
    static std::atomic<std::uint64_t> sequence{0U};
    try {
        const std::filesystem::path directory = CrashDumpDirectory();
        std::filesystem::create_directories(directory);
        const std::uint64_t index = sequence.fetch_add(1U, std::memory_order_relaxed);
        const std::wstring filename = L"VantaEngine-" +
            std::to_wstring(GetCurrentProcessId()) + L"-" +
            std::to_wstring(index) + L".nv-gpudmp";
        std::ofstream output(directory / filename, std::ios::binary | std::ios::out);
        if (output)
            output.write(static_cast<const char*>(dump), static_cast<std::streamsize>(size));
    } catch (...) {
        // Crash callbacks must never allow filesystem exceptions to escape.
    }
}
#endif

void NvidiaAftermathRuntime::Shutdown() noexcept
{
#if defined(VANTA_ENABLE_AFTERMATH)
    if (m_status.crashDumpsEnabled)
        (void)GFSDK_Aftermath_DisableGpuCrashDumps();
#endif
    m_status.crashDumpsEnabled = false;
    m_status.deviceInitialized = false;
}

} // namespace vanta::renderer
