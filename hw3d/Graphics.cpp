#include "Graphics.h"
#include <sstream>
#include <d3dcompiler.h>

#pragma comment(lib, "D3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "D3DCompiler.lib")

using namespace Microsoft::WRL;

// graphics exception checking/throwing macros (some with dxgi infos)
#define GFX_EXCEPT_NOINFO(hr) Graphics::HrException( __LINE__,__FILE__,(hr) )
#define GFX_THROW_NOINFO(hrcall) if( FAILED( hr = (hrcall) ) ) throw Graphics::HrException( __LINE__,__FILE__,hr )

#ifndef NDEBUG
#define GFX_EXCEPT(hr) Graphics::HrException( __LINE__,__FILE__,(hr),infoManager.GetMessages() )
#define GFX_THROW_INFO(hrcall) infoManager.Set(); if( FAILED( hr = (hrcall) ) ) throw GFX_EXCEPT(hr)
#define GFX_DEVICE_REMOVED_EXCEPT(hr) Graphics::DeviceRemovedException( __LINE__,__FILE__,(hr),infoManager.GetMessages() )
#else
#define GFX_EXCEPT(hr) Graphics::HrException( __LINE__,__FILE__,(hr) )
#define GFX_THROW_INFO(hrcall) GFX_THROW_NOINFO(hrcall)
#define GFX_DEVICE_REMOVED_EXCEPT(hr) Graphics::DeviceRemovedException( __LINE__,__FILE__,(hr) )
#endif

Graphics::Graphics(HWND hWnd)
    : m_FrameIndex(0),
    m_rtvDescriptorSize(0),
    m_Viewport(0.0f, 0.0f, 800, 600),
    m_ScissorRect(0, 0, 800, 600)
{
    // Load the rendering pipeline dependencies. 
    uint32_t dxgiFactoryFlags = 0;

#if defined(_DEBUG)
    // Enable the debug layer (requires the Graphics Tools "optional feature").
    // NOTE: Enabling the debug layer after the device creation will invalidate the active device.
    {
        Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer();

            // Enable additional debug layers.
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#endif

    // for checking results of d3d functions
    HRESULT hr;

    // Create device.
    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    GFX_THROW_INFO(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

    Microsoft::WRL::ComPtr<IDXGIAdapter1> hardwareAdapter;
    GetHardwareAdapter(factory.Get(), &hardwareAdapter);

    GFX_THROW_INFO(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_Device)));

    // Describe and create the command queue.
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    GFX_THROW_INFO(m_Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_CommandQueue)));

    // Describe and create the swap chain.
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = FrameCount;
    swapChainDesc.Width = 0;
    swapChainDesc.Height = 0;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain;
    GFX_THROW_INFO(factory->CreateSwapChainForHwnd(
        m_CommandQueue.Get(),     // Swap chain needs the queue so that it can force a flush
        hWnd,
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain
    ));

    // This application does not support fullscreen transitions. 
    GFX_THROW_INFO(factory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER));

    // "Inherit" lesser swap chain into greater member class swap chain object. 
    GFX_THROW_INFO(swapChain.As(&m_SwapChain));
    m_FrameIndex = m_SwapChain->GetCurrentBackBufferIndex();

    // Create descriptor heaps.
    {
        // Describe and create a render target view (RTV) descriptor heap.
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = FrameCount;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        GFX_THROW_INFO(m_Device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

        m_rtvDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

       // Describe and create a constant buffer view (CBV) descriptor heap.
       // Flags indicate that this descriptor heap can be bound to the pipeline 
       // and that descriptors contained in it can be referenced by a root table.
        D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc = {};
        cbvHeapDesc.NumDescriptors = 1;
        cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        GFX_THROW_INFO(m_Device->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&m_cbvHeap)));
    }

    // Create frame resources.
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

        // Create a RTV for each frame.
        for (uint32_t n = 0; n < FrameCount; n++)
        {
            GFX_THROW_INFO(m_SwapChain->GetBuffer(n, IID_PPV_ARGS(&m_RenderTargets[n])));
            m_Device->CreateRenderTargetView(m_RenderTargets[n].Get(), nullptr, rtvHandle);
            rtvHandle.Offset(1, m_rtvDescriptorSize);
        }
    }

    GFX_THROW_INFO(m_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_CommandAllocator)));

    // Create synchronization assets.
    {
        GFX_THROW_INFO(m_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_Fence)));
        m_FenceValue = 1;

        // Create an event handle to use for frame synchronization.
        m_FenceEvent = CreateEvent(nullptr, false, false, nullptr);
        if (m_FenceEvent == nullptr)
        {
            HRESULT_FROM_WIN32(GetLastError());
        }
    }
}

Graphics::~Graphics()
{
    // Ensure that the GPU is no longer referencing resources that are about to be
    // cleaned up by the destructor.
    WaitForPreviousFrame();

    CloseHandle(m_FenceEvent);
}

void Graphics::EndFrame()
{
    HRESULT hr;

    //CreateTestTriangle();

    // Record all the commands we need to render the scene into the command list.
    PopulateCommandList();

    // Execute the command list.
    ID3D12CommandList* ppCommandLists[] = { m_CommandList.Get() };
    m_CommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Present the frame.
    if (FAILED(hr = m_SwapChain->Present(1u, 0)))
    {
        if (hr == DXGI_ERROR_DEVICE_REMOVED)
        {
            throw GFX_DEVICE_REMOVED_EXCEPT(m_Device->GetDeviceRemovedReason());
        }
        else
        {
            GFX_THROW_INFO(hr);
        }
    }

    WaitForPreviousFrame();
}

void Graphics::ClearBuffer(float red, float green, float blue, float alpha)
{
    m_Color = { red, green, blue, alpha };
}

void Graphics::CreateTestTriangle(float angle)
{
    HRESULT hr;

    // Create a root signature consisting of a descriptor table with a single CBV.
    {
        D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

        // This is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion returned will not be greater than this.
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

        if (FAILED(m_Device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
        {
            featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
        }

        CD3DX12_DESCRIPTOR_RANGE1 ranges[1];
        CD3DX12_ROOT_PARAMETER1 rootParameters[1];

        ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
        rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_VERTEX);

        // Allow input layout and deny uneccessary access to certain pipeline stages.
        D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
            D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 0, nullptr, rootSignatureFlags);

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        GFX_THROW_INFO(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
        GFX_THROW_INFO(m_Device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_RootSignature)));
    }

    // Create the pipeline state, which includes compiling and loading shaders.
    {
        ComPtr<ID3DBlob> vertexShader;
        ComPtr<ID3DBlob> pixelShader;

#if defined(_DEBUG)
        // Enable better shader debugging with the graphics debugging tools.
        UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        UINT compileFlags = 0;
#endif

        GFX_THROW_INFO(D3DCompileFromFile(L"Vertex.hlsl", nullptr, nullptr, "main", "vs_5_0", compileFlags, 0, &vertexShader, nullptr));
        GFX_THROW_INFO(D3DCompileFromFile(L"Pixel.hlsl", nullptr, nullptr, "main", "ps_5_0", compileFlags, 0, &pixelShader, nullptr));

        // Define the vertex input layout.
        D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        // Describe and create the graphics pipeline state object (PSO).
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
        psoDesc.pRootSignature = m_RootSignature.Get();
        psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
        psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState.DepthEnable = FALSE;
        psoDesc.DepthStencilState.StencilEnable = FALSE;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.SampleDesc.Count = 1;
        GFX_THROW_INFO(m_Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_PipelineState)));
    }

    // Create the command list.
    GFX_THROW_INFO(m_Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_CommandAllocator.Get(), nullptr, IID_PPV_ARGS(&m_CommandList)));

    // Command lists are created in the recording state, but there is nothing
    // to record yet. The main loop expects it to be closed, so close it now.
    GFX_THROW_INFO(m_CommandList->Close());

    // Create the vertex buffer.
    {
        struct Vertex
        {
            struct
            {
                float x;
                float y;
            } pos;
            struct 
            {
                unsigned char r;
                unsigned char g;
                unsigned char b;
                unsigned char a;
            } color; 
        };
        // Define the geometry for a triangle.
        Vertex triangleVertices[] =
        {
            { 0.0f, 0.5f, 255, 0, 0, 0 },
            { 0.5f, -0.5f, 0, 255, 0, 0 },
            { -0.5f, -0.5f, 0, 0, 255, 0 },
            { -0.3f, 0.3f, 0, 255, 0, 0 },
            { 0.3f, 0.3f, 0, 0, 255, 0 },
            { 0.0f, -1.0f, 255, 0, 0, 0 },
        };

        triangleVertices[0].color.g = 255;

        const UINT vertexBufferSize = sizeof(triangleVertices);
        triangleSize = static_cast<uint32_t>(std::size(triangleVertices));

        // Note: using upload heaps to transfer static data like vert buffers is not 
        // recommended. Every time the GPU needs it, the upload heap will be marshalled 
        // over. Please read up on Default Heap usage. An upload heap is used here for 
        // code simplicity and because there are very few verts to actually transfer.
        GFX_THROW_INFO(m_Device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_VertexBuffer)));

        // Copy the triangle data to the vertex buffer.
        UINT8* pVertexDataBegin;
        CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
        GFX_THROW_INFO(m_VertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
        memcpy(pVertexDataBegin, triangleVertices, sizeof(triangleVertices));
        m_VertexBuffer->Unmap(0, nullptr);

        // Initialize the vertex buffer view.
        m_VertexBufferView.BufferLocation = m_VertexBuffer->GetGPUVirtualAddress();
        m_VertexBufferView.StrideInBytes = sizeof(Vertex);
        m_VertexBufferView.SizeInBytes = vertexBufferSize;
    }

    // Create the index buffer.
    {
        const unsigned short indices[] =
        {
            0, 1, 2,
            0, 2, 3,
            0, 4, 1,
            2, 1, 5,
        };
        const UINT indexBufferSize = sizeof(indices);
        indexSize = static_cast<uint32_t>(std::size(indices));

        GFX_THROW_INFO(m_Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_IndexBuffer)));

        // Copy the triangle data to the vertex buffer.
        UINT8* pIndexDataBegin;
        CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
        GFX_THROW_INFO(m_IndexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pIndexDataBegin)));
        memcpy(pIndexDataBegin, indices, sizeof(indices));
        m_IndexBuffer->Unmap(0, nullptr);

        // Initialize the vertex buffer view.
        m_IndexBufferView.BufferLocation = m_IndexBuffer->GetGPUVirtualAddress();
        m_IndexBufferView.Format = DXGI_FORMAT_R16_UINT;
        m_IndexBufferView.SizeInBytes = indexBufferSize;
    }

    // Create constant buffer. 
    {
        struct ConstantBuffer
        {
            struct
            {
                float element[4][4];
                float padding[48]; // Constant buffer must be 256 byte aligned. 
            } transformation;
        };

        ConstantBuffer cb =
        {
            {
                std::cos(angle),  std::sin(angle), 0.0f, 0.0f,
                -std::sin(angle), std::cos(angle), 0.0f, 0.0f,
                0.0f,             0.0f,            1.0f, 0.0f,
                0.0f,             0.0f,            0.0f, 1.0f,
            }
        };

        const uint32_t cbvSize = sizeof(ConstantBuffer);

        GFX_THROW_INFO(m_Device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(cbvSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr, IID_PPV_ARGS(&m_ConstantBuffer)
        ));

        // Describe and create a constant buffer view.
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = m_ConstantBuffer->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = cbvSize;
        m_Device->CreateConstantBufferView(&cbvDesc, m_cbvHeap->GetCPUDescriptorHandleForHeapStart());

        // Map and initialize the constant buffer. We don't unmap this until the
        // app closes. Keeping things mapped for the lifetime of the resource is okay.
        CD3DX12_RANGE readRange(0, 0); // Not reading from resource on CPU
        GFX_THROW_INFO(m_ConstantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pCbvDataBegin)));
        memcpy(pCbvDataBegin, &cb, sizeof(cb));
        
    }
    // Wait for the command list to execute; we are reusing the same command 
    // list in our main loop but for now, we just want to wait for setup to 
    // complete before continuing.
    WaitForPreviousFrame();
}

void Graphics::PopulateCommandList()
{
    HRESULT hr;

    // Command list allocators can only be reset when the associated
    // command lists have finished execution on the GPU; apps should use
    // fences to determine GPU execution progress.
    GFX_THROW_INFO(m_CommandAllocator->Reset());

    // However, when ExecuteCommandList() is called on a particular command
    // list, that command list can then be reset at any time and must be before
    // re-recording.
    GFX_THROW_INFO(m_CommandList->Reset(m_CommandAllocator.Get(), m_PipelineState.Get()));

    // Set necessary state.
    m_CommandList->SetGraphicsRootSignature(m_RootSignature.Get());

    ID3D12DescriptorHeap* ppHeaps[] = { m_cbvHeap.Get() };
    m_CommandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

    m_CommandList->SetGraphicsRootDescriptorTable(0, m_cbvHeap->GetGPUDescriptorHandleForHeapStart());
    m_CommandList->RSSetViewports(1, &m_Viewport);
    m_CommandList->RSSetScissorRects(1, &m_ScissorRect);

    // Indicate that the back buffer will be used as a render target.
    m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_RenderTargets[m_FrameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_FrameIndex, m_rtvDescriptorSize);
    m_CommandList->OMSetRenderTargets(1, &rtvHandle, false, nullptr);

    // Record commands.
    m_CommandList->ClearRenderTargetView(rtvHandle, (FLOAT*)&m_Color, 0, nullptr);
    m_CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_CommandList->IASetVertexBuffers(0, 1, &m_VertexBufferView);
    m_CommandList->IASetIndexBuffer(&m_IndexBufferView);
    m_CommandList->DrawIndexedInstanced((UINT)indexSize, 1, 0,0 , 0);

    // Indicate that the back buffer will now be used to present.
    m_CommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_RenderTargets[m_FrameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    GFX_THROW_INFO(m_CommandList->Close());
}

void Graphics::WaitForPreviousFrame()
{
   // WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
   // This is code implemented as such for simplicity. The D3D12HelloFrameBuffering
   // sample illustrates how to use fences for efficient resource usage and to
   // maximize GPU utilization.

    HRESULT hr;

   // Signal and increment the fence value.
    const UINT64 fence = m_FenceValue;
    m_CommandQueue->Signal(m_Fence.Get(), fence);
    m_FenceValue++;

    // Wait until the previous frame is finished.
    if (m_Fence->GetCompletedValue() < fence)
    {
        GFX_THROW_INFO(m_Fence->SetEventOnCompletion(fence, m_FenceEvent));
        WaitForSingleObject(m_FenceEvent, INFINITE);
    }

    m_FrameIndex = m_SwapChain->GetCurrentBackBufferIndex();
}

void Graphics::GetHardwareAdapter(IDXGIFactory4* pFactory, IDXGIAdapter1** ppAdapter)
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

// Graphics exception stuff
std::string Graphics::Exception::TranslateErrorCode(HRESULT hr) noexcept
{
    char* pMsgBuf = nullptr;
    // windows will allocate memory for err string and make our pointer point to it
    const DWORD nMsgLen = FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, hr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&pMsgBuf), 0, nullptr
    );
    // 0 string length returned indicates a failure
    if (nMsgLen == 0)
    {
        return "Unidentified error code";
    }
    // copy error string from windows-allocated buffer to std::string
    std::string errorString = pMsgBuf;
    // free windows buffer
    LocalFree(pMsgBuf);
    return errorString;
}

Graphics::HrException::HrException(int line, const char* file, HRESULT hr, std::vector<std::string> infoMsgs) noexcept
    :
    Exception(line, file),
    hr(hr)
{
    // join all info messages with newlines into single string
    for (const auto& m : infoMsgs)
    {
        info += m;
        info.push_back('\n');
    }
    // remove final newline if exists
    if (!info.empty())
    {
        info.pop_back();
    }
}

const char* Graphics::HrException::what() const noexcept
{
    std::ostringstream oss;
    oss << GetType() << std::endl
        << "[Error Code] 0x" << std::hex << std::uppercase << GetErrorCode()
        << std::dec << " (" << (unsigned long)GetErrorCode() << ")" << std::endl
        << "[Error String] " << GetErrorString() << std::endl
        << "[Description] " << GetErrorDescription() << std::endl;
    if (!info.empty())
    {
        oss << "\n[Error Info]\n" << GetErrorInfo() << std::endl << std::endl;
    }
    oss << GetOriginString();
    whatBuffer = oss.str();
    return whatBuffer.c_str();
}

std::string Graphics::HrException::GetErrorInfo() const noexcept
{
    return info;
}


const char* Graphics::HrException::GetType() const noexcept
{
    return "Chili Graphics Exception";
}

HRESULT Graphics::HrException::GetErrorCode() const noexcept
{
    return hr;
}

HRESULT Graphics::HrException::GetErrorString() const noexcept
{
    return hr;
}

std::string Graphics::HrException::GetErrorDescription() const noexcept
{
    return Exception::TranslateErrorCode(hr);
}


const char* Graphics::DeviceRemovedException::GetType() const noexcept
{
    return "Chili Graphics Exception [Device Removed] (DXGI_ERROR_DEVICE_REMOVED)";
}