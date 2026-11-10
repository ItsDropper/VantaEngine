#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_3.h>

#include <string>

namespace vanta::renderer
{
struct NvidiaStreamlineStatus
{
    bool sdkCompiledIn = false;
    bool initialized = false;
    bool deviceAttached = false;
    bool dlssSupported = false;
    bool reflexSupported = false;
    bool latencyMarkersSupported = false;
    bool reflexEnabled = false;
    bool dlssFrameGenerationSupported = false;
    bool dlssRayReconstructionSupported = false;
    bool nvidiaImageScalingSupported = false;
    std::string diagnostic;
};

// Optional real NVIDIA Streamline runtime. When the SDK is not configured,
// the engine remains fully functional and uses its native renderer fallback.
class NvidiaStreamlineRuntime final
{
public:
    NvidiaStreamlineRuntime() = default;
    ~NvidiaStreamlineRuntime();

    NvidiaStreamlineRuntime(const NvidiaStreamlineRuntime&) = delete;
    NvidiaStreamlineRuntime& operator=(const NvidiaStreamlineRuntime&) = delete;

    [[nodiscard]] bool Initialize(std::string& error);
    [[nodiscard]] HRESULT CreateDXGIFactory2(UINT flags, REFIID interfaceId, void** factory) const noexcept;
    [[nodiscard]] HRESULT CreateD3D12Device(IUnknown* adapter, D3D_FEATURE_LEVEL minimumFeatureLevel,
        REFIID interfaceId, void** device) const noexcept;
    [[nodiscard]] bool SetDevice(ID3D12Device* device, const LUID& adapterLuid,
        std::string& error);
    void BeginFrame(std::uint32_t frameIndex) noexcept;
    void MarkRenderSubmitStart() noexcept;
    void MarkRenderSubmitEnd() noexcept;
    void MarkPresentStart() noexcept;
    void MarkPresentEnd() noexcept;
    void Shutdown() noexcept;

    [[nodiscard]] const NvidiaStreamlineStatus& Status() const noexcept { return m_status; }

private:
    NvidiaStreamlineStatus m_status;
    HMODULE m_streamlineModule = nullptr;
    FARPROC m_createFactory2 = nullptr;
    FARPROC m_createD3D12Device = nullptr;
    FARPROC m_slInit = nullptr;
    FARPROC m_slShutdown = nullptr;
    FARPROC m_slSetD3DDevice = nullptr;
    FARPROC m_slIsFeatureSupported = nullptr;
    FARPROC m_slGetNewFrameToken = nullptr;
    FARPROC m_slGetFeatureFunction = nullptr;
    FARPROC m_slPCLSetMarker = nullptr;
    FARPROC m_slReflexSetOptions = nullptr;
    void* m_currentFrameToken = nullptr;
};
} // namespace vanta::renderer
