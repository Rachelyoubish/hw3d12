#pragma once
#include "ChiliWin.h"
#include "d3dx12.h"

#include <d3d12.h>
#include <DirectXMath.h>
#include <dxgi1_6.h>
#include <wrl.h>

#include <stdint.h>

class Graphics
{
public:
    Graphics(HWND hWnd);
    Graphics(const Graphics&) = delete; // Delete copy.
    Graphics& operator=(const Graphics&) = delete; // Delete assignment.
    ~Graphics();
    void EndFrame();
    void ClearBuffer(float red, float green, float blue, float alpha = 1.0f)
    {
        m_Color = { red, green, blue, alpha };
    }
    DirectX::XMFLOAT4 m_Color;
private:
    void PopulateCommandList();
    void WaitForPreviousFrame();
    // Function from MSDN
    // Source: https://docs.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-d3d12createdevice
    void GetHardwareAdapter(IDXGIFactory4* pFactory, IDXGIAdapter1** ppAdapter)
    {
        *ppAdapter = nullptr;
        for (UINT adapterIndex = 0; ; ++adapterIndex)
        {
            IDXGIAdapter1* pAdapter = nullptr;
            if (DXGI_ERROR_NOT_FOUND == pFactory->EnumAdapters1(adapterIndex, &pAdapter))
            {
                // No more adapters to enumerate.
                break;
            }

            // Check to see if the adapter supports Direct3D 12, but don't create the
            // actual device yet.
            if (SUCCEEDED(D3D12CreateDevice(pAdapter, D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
            {
                *ppAdapter = pAdapter;
                return;
            }
            pAdapter->Release();
        }
    }
private:
    static const uint32_t FrameCount = 2;

    // Pipeline objects.
    Microsoft::WRL::ComPtr<ID3D12Device> m_Device;
    Microsoft::WRL::ComPtr<IDXGISwapChain4> m_SwapChain;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_RenderTargets[FrameCount];
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_CommandAllocator;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_CommandQueue;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_PipelineState;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_CommandList;
    uint32_t m_rtvDescriptorSize;

    // Synchronization objects.
    uint32_t m_FrameIndex;
    HANDLE m_FenceEvent;
    Microsoft::WRL::ComPtr<ID3D12Fence> m_Fence;
    uint64_t m_FenceValue;
};