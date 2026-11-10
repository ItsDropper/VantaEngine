#include "vanta/renderer/NvidiaStreamline.hpp"

#include "vanta/core/Log.hpp"

#include <cstdint>
#include <iterator>
#include <string>

#if defined(VANTA_ENABLE_STREAMLINE)
#include <sl.h>
#include <sl_consts.h>
#include <sl_dlss.h>
#include <sl_reflex.h>
#endif

namespace vanta::renderer
{
#if defined(VANTA_ENABLE_STREAMLINE)
namespace
{
void StreamlineLogMessage(const sl::LogType type, const char* message)
{
    if (message == nullptr) return;
    if (type == sl::LogType::eError)
        LogError(message);
    else if (type == sl::LogType::eWarn)
        LogInfo(message);
}

sl::AdapterInfo MakeAdapterInfo(const LUID& adapterLuid) noexcept
{
    sl::AdapterInfo info{};
    info.deviceLUID = reinterpret_cast<std::uint8_t*>(
        const_cast<LUID*>(&adapterLuid));
    info.deviceLUIDSizeInBytes = sizeof(adapterLuid);
    return info;
}
} // namespace
#endif

NvidiaStreamlineRuntime::~NvidiaStreamlineRuntime()
{
    Shutdown();
}

bool NvidiaStreamlineRuntime::Initialize(std::string& error)
{
    Shutdown();
#if defined(VANTA_ENABLE_STREAMLINE)
    m_status.sdkCompiledIn = true;

    constexpr sl::Feature features[] = {
        sl::kFeatureDLSS,
        sl::kFeatureReflex
    };
    sl::Preferences preferences{};
    preferences.engine = sl::EngineType::eCustom;
    preferences.engineVersion = "0.2.0";
    preferences.renderAPI = sl::RenderAPI::eD3D12;
    preferences.featuresToLoad = features;
    preferences.numFeaturesToLoad = static_cast<std::uint32_t>(std::size(features));
    preferences.flags = sl::PreferenceFlag::eDisableCLStateTracking;
    preferences.logMessageCallback = StreamlineLogMessage;

    const sl::Result result = slInit(preferences);
    if (result != sl::Result::eOk) {
        error = "NVIDIA Streamline initialization failed (result " +
            std::to_string(static_cast<std::uint32_t>(result)) + ").";
        m_status.diagnostic = error;
        return false;
    }

    m_status.initialized = true;
    m_status.diagnostic = "NVIDIA Streamline initialized; waiting for the selected D3D12 device.";
    error.clear();
    return true;
#else
    m_status.diagnostic = "NVIDIA Streamline SDK is not configured in this build.";
    error = m_status.diagnostic;
    return false;
#endif
}

bool NvidiaStreamlineRuntime::SetDevice(
    ID3D12Device* device, const LUID& adapterLuid, std::string& error)
{
#if defined(VANTA_ENABLE_STREAMLINE)
    if (!m_status.initialized) {
        error = "Cannot attach a D3D12 device before NVIDIA Streamline initialization.";
        return false;
    }
    if (device == nullptr) {
        error = "Cannot attach a null D3D12 device to NVIDIA Streamline.";
        return false;
    }

    const sl::Result attachResult = slSetD3DDevice(device);
    if (attachResult != sl::Result::eOk) {
        error = "NVIDIA Streamline could not attach to the selected D3D12 device (result " +
            std::to_string(static_cast<std::uint32_t>(attachResult)) + ").";
        m_status.diagnostic = error;
        return false;
    }

    const sl::AdapterInfo adapterInfo = MakeAdapterInfo(adapterLuid);
    const sl::Result dlssResult = slIsFeatureSupported(sl::kFeatureDLSS, adapterInfo);
    const sl::Result reflexResult = slIsFeatureSupported(sl::kFeatureReflex, adapterInfo);
    m_status.deviceAttached = true;
    m_status.dlssSupported = dlssResult == sl::Result::eOk;
    m_status.reflexSupported = reflexResult == sl::Result::eOk;

    m_status.diagnostic = "NVIDIA Streamline device attached. DLSS plugin: " +
        std::string(m_status.dlssSupported ? "supported" : "unavailable") +
        "; Reflex plugin: " +
        (m_status.reflexSupported ? "supported" : "unavailable") +
        ". Feature execution still requires render-pipeline resource tagging and evaluation.";
    error.clear();
    return true;
#else
    (void)device;
    (void)adapterLuid;
    error = "NVIDIA Streamline SDK is not configured in this build.";
    return false;
#endif
}

void NvidiaStreamlineRuntime::Shutdown() noexcept
{
#if defined(VANTA_ENABLE_STREAMLINE)
    if (m_status.initialized)
        (void)slShutdown();
#endif
    m_status.initialized = false;
    m_status.deviceAttached = false;
    m_status.dlssSupported = false;
    m_status.reflexSupported = false;
}
} // namespace vanta::renderer
