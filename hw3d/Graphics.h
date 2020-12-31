#pragma once
#include "ChiliWin.h"
#include "ChiliException.h"

#include "DxgiInfoManager.h"
#include "d3dx12.h"

#include <d3d12.h>
#include <DirectXMath.h>
#include <dxgi1_6.h>
#include <wrl.h>

#include <stdint.h>
#include <vector>

class Graphics
{
public:
    class Exception : public ChiliException
    {
        using ChiliException::ChiliException;
    public:
        static std::string TranslateErrorCode(HRESULT hr) noexcept;
    };
    class HrException : public Exception
    {
    public:
        HrException(int line, const char* file, HRESULT hr, std::vector<std::string> infoMsgs = {}) noexcept;
        const char* what() const noexcept override;
        const char* GetType() const noexcept override;
        HRESULT GetErrorCode() const noexcept;
        HRESULT GetErrorString() const noexcept;
        std::string GetErrorDescription() const noexcept;
        std::string GetErrorInfo() const noexcept;
    private:
        HRESULT hr;
        std::string info;
    };
    class DeviceRemovedException : public HrException
    {
        using HrException::HrException;
    public:
        const char* GetType() const noexcept override;
    private:
        std::string reason;
    };
public:
    Graphics(HWND hWnd);
    Graphics(const Graphics&) = delete; // Delete copy.
    Graphics& operator=(const Graphics&) = delete; // Delete assignment.
    ~Graphics();
    void EndFrame();
    void ClearBuffer(float red, float green, float blue, float alpha = 1.0f);
    void CreateTestTriangle();
    DirectX::XMFLOAT4 m_Color;
    void PopulateCommandList();
    void WaitForPreviousFrame();
    // Function from MSDN
    // Source: https://docs.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-d3d12createdevice
    void GetHardwareAdapter(IDXGIFactory4* pFactory, IDXGIAdapter1** ppAdapter);
private:
    static const uint32_t FrameCount = 2;
    uint32_t triangleSize = 0;
    uint32_t indexSize = 0;

#ifndef NDEBUG
    DxgiInfoManager infoManager;
#endif
    // Pipeline objects.
    CD3DX12_VIEWPORT m_Viewport;
    CD3DX12_RECT m_ScissorRect;
    Microsoft::WRL::ComPtr<ID3D12Device> m_Device;
    Microsoft::WRL::ComPtr<IDXGISwapChain4> m_SwapChain;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_RenderTargets[FrameCount];
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_CommandAllocator;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_CommandQueue;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_RootSignature;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_PipelineState;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_CommandList;
    uint32_t m_rtvDescriptorSize;

    // App resources.
    Microsoft::WRL::ComPtr<ID3D12Resource> m_VertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_VertexBufferView;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_IndexBuffer;
    D3D12_INDEX_BUFFER_VIEW m_IndexBufferView;

    // Synchronization objects.
    uint32_t m_FrameIndex;
    HANDLE m_FenceEvent;
    Microsoft::WRL::ComPtr<ID3D12Fence> m_Fence;
    uint64_t m_FenceValue;
};