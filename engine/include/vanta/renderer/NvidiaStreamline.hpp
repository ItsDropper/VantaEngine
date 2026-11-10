#pragma once

#include <Windows.h>
#include <d3d12.h>

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
    void* m_currentFrameToken = nullptr;
};
} // namespace vanta::renderer
