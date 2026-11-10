#include "vanta/renderer/NvidiaStreamline.hpp"

#include "vanta/core/Log.hpp"

#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

#if defined(VANTA_ENABLE_STREAMLINE)
#include <filesystem>
#include <softpub.h>
#include <wintrust.h>
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
using CreateFactory2Function = HRESULT(WINAPI*)(UINT, REFIID, void**);
using CreateD3D12DeviceFunction = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
using SlInitFunction = decltype(&slInit);
using SlShutdownFunction = decltype(&slShutdown);
using SlSetD3DDeviceFunction = decltype(&slSetD3DDevice);
using SlIsFeatureSupportedFunction = decltype(&slIsFeatureSupported);
using SlGetNewFrameTokenFunction = decltype(&slGetNewFrameToken);
using SlGetFeatureFunction = decltype(&slGetFeatureFunction);
using SlPclSetMarkerFunction = decltype(&slPCLSetMarker);
using SlReflexSetOptionsFunction = decltype(&slReflexSetOptions);

bool VerifyAuthenticodeSignature(const std::filesystem::path& path)
{
    WINTRUST_FILE_INFO fileInfo{};
    fileInfo.cbStruct = sizeof(fileInfo);
    fileInfo.pcwszFilePath = path.c_str();
    WINTRUST_DATA trustData{};
    trustData.cbStruct = sizeof(trustData);
    trustData.dwUIChoice = WTD_UI_NONE;
    trustData.fdwRevocationChecks = WTD_REVOKE_NONE;
    trustData.dwUnionChoice = WTD_CHOICE_FILE;
    trustData.pFile = &fileInfo;
    trustData.dwStateAction = WTD_STATEACTION_VERIFY;
    trustData.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG result = WinVerifyTrust(nullptr, &policy, &trustData);
    trustData.dwStateAction = WTD_STATEACTION_CLOSE;
    (void)WinVerifyTrust(nullptr, &policy, &trustData);
    return result == ERROR_SUCCESS;
}

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

    std::vector<wchar_t> executablePath(32768U, L'\0');
    const DWORD pathLength = GetModuleFileNameW(nullptr, executablePath.data(),
        static_cast<DWORD>(executablePath.size()));
    if (pathLength == 0U || pathLength >= executablePath.size()) {
        error = "Could not resolve the executable path for secure Streamline loading.";
        m_status.diagnostic = error;
        return false;
    }
    const std::filesystem::path interposerPath =
        std::filesystem::path(executablePath.data()).parent_path() / L"sl.interposer.dll";
    if (!VerifyAuthenticodeSignature(interposerPath)) {
        error = "NVIDIA Streamline interposer failed Windows Authenticode verification; refusing to load it.";
        m_status.diagnostic = error;
        return false;
    }
    m_streamlineModule = LoadLibraryExW(interposerPath.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (m_streamlineModule == nullptr) {
        error = "Could not load signed NVIDIA Streamline interposer (Win32 error " +
            std::to_string(GetLastError()) + ").";
        m_status.diagnostic = error;
        return false;
    }
    m_createFactory2 = GetProcAddress(m_streamlineModule, "CreateDXGIFactory2");
    m_createD3D12Device = GetProcAddress(m_streamlineModule, "D3D12CreateDevice");
    m_slInit = GetProcAddress(m_streamlineModule, "slInit");
    m_slShutdown = GetProcAddress(m_streamlineModule, "slShutdown");
    m_slSetD3DDevice = GetProcAddress(m_streamlineModule, "slSetD3DDevice");
    m_slIsFeatureSupported = GetProcAddress(m_streamlineModule, "slIsFeatureSupported");
    m_slGetNewFrameToken = GetProcAddress(m_streamlineModule, "slGetNewFrameToken");
    m_slGetFeatureFunction = GetProcAddress(m_streamlineModule, "slGetFeatureFunction");
    if (m_createFactory2 == nullptr || m_createD3D12Device == nullptr ||
        m_slInit == nullptr || m_slShutdown == nullptr || m_slSetD3DDevice == nullptr ||
        m_slIsFeatureSupported == nullptr || m_slGetNewFrameToken == nullptr ||
        m_slGetFeatureFunction == nullptr) {
        error = "Signed Streamline interposer is missing required D3D12/core/feature-resolver exports.";
        m_status.diagnostic = error;
        Shutdown();
        m_status.sdkCompiledIn = true;
        return false;
    }

    constexpr sl::Feature features[] = {
        sl::kFeatureDLSS,
        sl::kFeatureDLSS_G,
        sl::kFeatureDLSS_RR,
        sl::kFeatureReflex,
        sl::kFeaturePCL,
        sl::kFeatureNIS
    };
    sl::Preferences preferences{};
    preferences.engine = sl::EngineType::eCustom;
    preferences.engineVersion = "0.2.0";
    preferences.renderAPI = sl::RenderAPI::eD3D12;
    preferences.featuresToLoad = features;
    preferences.numFeaturesToLoad = static_cast<std::uint32_t>(std::size(features));
    preferences.flags = sl::PreferenceFlags::eDisableCLStateTracking;
    preferences.logMessageCallback = StreamlineLogMessage;

    const auto initialize = reinterpret_cast<SlInitFunction>(m_slInit);
    const sl::Result result = initialize(preferences, sl::kSDKVersion);
    if (result != sl::Result::eOk) {
        error = "NVIDIA Streamline initialization failed (result " +
            std::to_string(static_cast<std::uint32_t>(result)) + ").";
        m_status.diagnostic = error;
        Shutdown();
        m_status.sdkCompiledIn = true;
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

HRESULT NvidiaStreamlineRuntime::CreateDXGIFactory2(
    const UINT flags, const REFIID interfaceId, void** factory) const noexcept
{
#if defined(VANTA_ENABLE_STREAMLINE)
    if (m_createFactory2 != nullptr)
        return reinterpret_cast<CreateFactory2Function>(m_createFactory2)(flags, interfaceId, factory);
#endif
    return ::CreateDXGIFactory2(flags, interfaceId, factory);
}

HRESULT NvidiaStreamlineRuntime::CreateD3D12Device(
    IUnknown* adapter, const D3D_FEATURE_LEVEL minimumFeatureLevel,
    const REFIID interfaceId, void** device) const noexcept
{
#if defined(VANTA_ENABLE_STREAMLINE)
    if (m_createD3D12Device != nullptr)
        return reinterpret_cast<CreateD3D12DeviceFunction>(m_createD3D12Device)(
            adapter, minimumFeatureLevel, interfaceId, device);
#endif
    return ::D3D12CreateDevice(adapter, minimumFeatureLevel, interfaceId, device);
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

    const auto setDevice = reinterpret_cast<SlSetD3DDeviceFunction>(m_slSetD3DDevice);
    const sl::Result attachResult = setDevice(device);
    if (attachResult != sl::Result::eOk) {
        error = "NVIDIA Streamline could not attach to the selected D3D12 device (result " +
            std::to_string(static_cast<std::uint32_t>(attachResult)) + ").";
        m_status.diagnostic = error;
        return false;
    }

    const auto isFeatureSupported =
        reinterpret_cast<SlIsFeatureSupportedFunction>(m_slIsFeatureSupported);
    const sl::AdapterInfo adapterInfo = MakeAdapterInfo(adapterLuid);
    const sl::Result dlssResult = isFeatureSupported(sl::kFeatureDLSS, adapterInfo);
    const sl::Result frameGenerationResult =
        isFeatureSupported(sl::kFeatureDLSS_G, adapterInfo);
    const sl::Result rayReconstructionResult =
        isFeatureSupported(sl::kFeatureDLSS_RR, adapterInfo);
    const sl::Result reflexResult = isFeatureSupported(sl::kFeatureReflex, adapterInfo);
    const sl::Result pclResult = isFeatureSupported(sl::kFeaturePCL, adapterInfo);
    const sl::Result nisResult = isFeatureSupported(sl::kFeatureNIS, adapterInfo);
    m_status.deviceAttached = true;
    m_status.dlssSupported = dlssResult == sl::Result::eOk;
    m_status.dlssFrameGenerationSupported = frameGenerationResult == sl::Result::eOk;
    m_status.dlssRayReconstructionSupported = rayReconstructionResult == sl::Result::eOk;
    m_status.reflexSupported = reflexResult == sl::Result::eOk;
    m_status.latencyMarkersSupported = pclResult == sl::Result::eOk;
    m_status.nvidiaImageScalingSupported = nisResult == sl::Result::eOk;

    const auto getFeatureFunction =
        reinterpret_cast<SlGetFeatureFunction>(m_slGetFeatureFunction);
    if (m_status.latencyMarkersSupported) {
        void* markerFunction = nullptr;
        if (getFeatureFunction(sl::kFeaturePCL, "slPCLSetMarker", markerFunction) == sl::Result::eOk)
            m_slPCLSetMarker = reinterpret_cast<FARPROC>(markerFunction);
        else
            m_status.latencyMarkersSupported = false;
    }
    if (m_status.reflexSupported) {
        void* reflexOptionsFunction = nullptr;
        if (getFeatureFunction(sl::kFeatureReflex, "slReflexSetOptions",
                reflexOptionsFunction) == sl::Result::eOk)
            m_slReflexSetOptions = reinterpret_cast<FARPROC>(reflexOptionsFunction);
        if (m_slReflexSetOptions != nullptr) {
            sl::ReflexOptions reflexOptions{};
            reflexOptions.mode = sl::ReflexMode::eLowLatency;
            const auto setReflexOptions =
                reinterpret_cast<SlReflexSetOptionsFunction>(m_slReflexSetOptions);
            m_status.reflexEnabled = setReflexOptions(reflexOptions) == sl::Result::eOk;
        }
    }

    m_status.diagnostic = "NVIDIA Streamline device attached. DLSS Super Resolution: " +
        std::string(m_status.dlssSupported ? "supported" : "unavailable") +
        "; DLSS Frame Generation: " +
        (m_status.dlssFrameGenerationSupported ? "supported" : "unavailable") +
        "; DLSS Ray Reconstruction: " +
        (m_status.dlssRayReconstructionSupported ? "supported" : "unavailable") +
        "; Reflex: " +
        (m_status.reflexSupported ? "supported" : "unavailable") +
        "; Reflex low-latency mode: " +
        (m_status.reflexEnabled ? "enabled" : "not enabled") +
        "; latency markers: " +
        (m_status.latencyMarkersSupported ? "available" : "unavailable") +
        "; NVIDIA Image Scaling: " +
        (m_status.nvidiaImageScalingSupported ? "supported" : "unavailable") +
        ". DLSS/NIS image processing still requires render-resource tagging and feature evaluation.";
    error.clear();
    return true;
#else
    (void)device;
    (void)adapterLuid;
    error = "NVIDIA Streamline SDK is not configured in this build.";
    return false;
#endif
}

void NvidiaStreamlineRuntime::BeginFrame(const std::uint32_t frameIndex) noexcept
{
#if defined(VANTA_ENABLE_STREAMLINE)
    m_currentFrameToken = nullptr;
    if (!m_status.initialized || !m_status.deviceAttached ||
        !m_status.latencyMarkersSupported || m_slPCLSetMarker == nullptr)
        return;
    sl::FrameToken* token = nullptr;
    const auto getNewFrameToken = reinterpret_cast<SlGetNewFrameTokenFunction>(m_slGetNewFrameToken);
    if (getNewFrameToken(token, &frameIndex) == sl::Result::eOk)
        m_currentFrameToken = token;
#else
    (void)frameIndex;
#endif
}

void NvidiaStreamlineRuntime::MarkRenderSubmitStart() noexcept
{
#if defined(VANTA_ENABLE_STREAMLINE)
    if (m_currentFrameToken != nullptr && m_slPCLSetMarker != nullptr) {
        const auto setMarker = reinterpret_cast<SlPclSetMarkerFunction>(m_slPCLSetMarker);
        (void)setMarker(sl::PCLMarker::eRenderSubmitStart,
            *static_cast<sl::FrameToken*>(m_currentFrameToken));
    }
#endif
}

void NvidiaStreamlineRuntime::MarkRenderSubmitEnd() noexcept
{
#if defined(VANTA_ENABLE_STREAMLINE)
    if (m_currentFrameToken != nullptr) {
        const auto setMarker = reinterpret_cast<SlPclSetMarkerFunction>(m_slPCLSetMarker);
        (void)setMarker(sl::PCLMarker::eRenderSubmitEnd,
            *static_cast<sl::FrameToken*>(m_currentFrameToken));
    }
#endif
}

void NvidiaStreamlineRuntime::MarkPresentStart() noexcept
{
#if defined(VANTA_ENABLE_STREAMLINE)
    if (m_currentFrameToken != nullptr) {
        const auto setMarker = reinterpret_cast<SlPclSetMarkerFunction>(m_slPCLSetMarker);
        (void)setMarker(sl::PCLMarker::ePresentStart,
            *static_cast<sl::FrameToken*>(m_currentFrameToken));
    }
#endif
}

void NvidiaStreamlineRuntime::MarkPresentEnd() noexcept
{
#if defined(VANTA_ENABLE_STREAMLINE)
    if (m_currentFrameToken != nullptr) {
        const auto setMarker = reinterpret_cast<SlPclSetMarkerFunction>(m_slPCLSetMarker);
        (void)setMarker(sl::PCLMarker::ePresentEnd,
            *static_cast<sl::FrameToken*>(m_currentFrameToken));
    }
    m_currentFrameToken = nullptr;
#endif
}

void NvidiaStreamlineRuntime::Shutdown() noexcept
{
#if defined(VANTA_ENABLE_STREAMLINE)
    if (m_status.initialized && m_slShutdown != nullptr) {
        const auto shutdown = reinterpret_cast<SlShutdownFunction>(m_slShutdown);
        (void)shutdown();
    }
    if (m_streamlineModule != nullptr) {
        FreeLibrary(m_streamlineModule);
        m_streamlineModule = nullptr;
    }
#endif
    m_status.initialized = false;
    m_status.deviceAttached = false;
    m_status.dlssSupported = false;
    m_status.reflexSupported = false;
    m_status.latencyMarkersSupported = false;
    m_status.reflexEnabled = false;
    m_currentFrameToken = nullptr;
    m_status.dlssFrameGenerationSupported = false;
    m_status.dlssRayReconstructionSupported = false;
    m_status.nvidiaImageScalingSupported = false;
    m_createFactory2 = nullptr;
    m_createD3D12Device = nullptr;
    m_slInit = nullptr;
    m_slShutdown = nullptr;
    m_slSetD3DDevice = nullptr;
    m_slIsFeatureSupported = nullptr;
    m_slGetNewFrameToken = nullptr;
    m_slGetFeatureFunction = nullptr;
    m_slPCLSetMarker = nullptr;
    m_slReflexSetOptions = nullptr;
}
} // namespace vanta::renderer
