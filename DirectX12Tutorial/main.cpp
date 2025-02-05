#include <iostream>
#include <string>
#include <Windows.h>
#include <comdef.h>
#include <wrl.h>
#include <fstream>
#include <direct.h>

#include "d3dx12.h"
#include <dxgi1_6.h>
#include <DirectXMath.h>
#include <d3d12.h>
#include <d3d12shader.h>
#include <d3dcompiler.h>
#include <array>
#ifdef _DEBUG
#include <dxgidebug.h>
#endif
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "d3dcompiler.lib")

using namespace DirectX;
using namespace Microsoft::WRL;

struct Vertex {
	DirectX::XMFLOAT3 position;
	DirectX::XMFLOAT4 color;
	Vertex(float x, float y, float z, float r = 0.f, float g = 0.f, float b = 0.f, float a = 1.f):
		position{x, y, z}, color{r, g, b, a}
	{

	}
};

#pragma region Prepare utilities


//AnsiToWString函数（转换成宽字符类型的字符串，wstring）
//在Windows平台上，我们应该都使用wstring和wchar_t，处理方式是在字符串前+L
inline std::wstring AnsiToWString(const std::string &str) {
	WCHAR buffer[512];
	MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, buffer, 512);
	return std::wstring(buffer);
}

//DxException类
class DxException {
public:
	DxException() = default;
	DxException(HRESULT hr, const std::wstring &functionName, const std::wstring &filename, int lineNumber) :
		ErrorCode(hr),
		FunctionName(functionName),
		Filename(filename),
		LineNumber(lineNumber) {
	}

	std::wstring ToString()const {
		// Get the string description of the error code.
		_com_error err(ErrorCode);
		std::wstring msg = err.ErrorMessage();

		return FunctionName + L" failed in " + Filename + L"; line " + std::to_wstring(LineNumber) + L"; errorcode: " + std::to_wstring(ErrorCode) + L"; errormsg : " + msg;

	}

	HRESULT ErrorCode = S_OK;
	std::wstring FunctionName;
	std::wstring Filename;
	int LineNumber = -1;
};


//宏定义ThrowIfFailed
#ifndef ThrowIfFailed
#define ThrowIfFailed(x)                                              \
{                                                                     \
    HRESULT hr__ = (x);                                               \
    std::wstring wfn = AnsiToWString(__FILE__);                       \
    if(FAILED(hr__)) { DxException ex(hr__, L#x, wfn, __LINE__); std::wcerr << ex.ToString() << std::endl; throw ex; } \
}
#endif



void WaitForPreviousFrame(UINT64 &m_fenceValue, ComPtr<ID3D12CommandQueue> &m_commandQueue, ComPtr<ID3D12Fence> &m_fence, HANDLE &m_fenceEvent, ComPtr<IDXGISwapChain3> &m_swapChain, int &m_frameIndex) {
	// WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
	// This is code implemented as such for simplicity. The D3D12HelloFrameBuffering
	// sample illustrates how to use fences for efficient resource usage and to
	// maximize GPU utilization.

	// Signal and increment the m_fence value.
	const UINT64 fence = m_fenceValue;
	ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fence));
	m_fenceValue++;

	// Wait until the previous frame is finished.
	if (m_fence->GetCompletedValue() < fence)
	{
		ThrowIfFailed(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
		WaitForSingleObject(m_fenceEvent, INFINITE);
	}

	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}


#pragma endregion

#include <chrono>
class GameTime {
	using Clock = std::chrono::high_resolution_clock;
public:
	GameTime() {
		
	}
	void Tick() {
		m_previousTime = m_currentTime;
		m_currentTime = Clock::now();
		m_deltaTime = m_currentTime - m_previousTime;
	}

	float DeltaTime() const {
		
	}
private:
	Clock::time_point m_currentTime;
	Clock::time_point m_previousTime;
	Clock::duration m_deltaTime;
};

class DirectX12 {

	void createWindow(int width, int height, LPCWSTR name) {
		WNDCLASSEX wc{ 0 };
		wc.cbSize = sizeof(WNDCLASSEX);
		wc.style = CS_HREDRAW | CS_VREDRAW;	//当工作区宽高改变，则重新绘制窗口
		wc.lpfnWndProc = wndProc;	//指定窗口过程
		wc.cbClsExtra = 0;	//借助这两个字段来为当前应用分配额外的内存空间（这里不分配，所以置0）
		wc.cbWndExtra = 0;	//借助这两个字段来为当前应用分配额外的内存空间（这里不分配，所以置0）
		wc.hInstance = GetModuleHandle(0);	//应用程序实例句柄（由WinMain传入）
		wc.hIcon = LoadIcon(0, IDI_APPLICATION);	//使用默认的应用程序图标
		wc.hCursor = LoadCursor(0, IDC_ARROW);	//使用标准的鼠标指针样式
		wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
		wc.lpszMenuName = 0;	//没有菜单栏
		wc.lpszClassName = name;	//窗口名
		wc.hIconSm = LoadIcon(0, IDI_APPLICATION);
		//窗口类注册失败
		if (!RegisterClassEx(&wc)) {
			//消息框函数，参数1：消息框所属窗口句柄，可为NULL。参数2：消息框显示的文本信息。参数3：标题文本。参数4：消息框样式
			throw std::logic_error("RegisterClass Failed");
		}

		//窗口类注册成功
		RECT R;	//裁剪矩形
		R.left = 0;
		R.top = 0;
		R.right = width;
		R.bottom = height;
		AdjustWindowRect(&R, WS_OVERLAPPEDWINDOW, false);	//根据窗口的客户区大小计算窗口的大小

		//创建窗口,返回布尔值
		m_handler = CreateWindow(wc.lpszClassName, name, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, width, height, 0, 0, GetModuleHandle(0), this);
		//窗口创建失败
		if (!m_handler) {
			throw std::logic_error("CreatWindow Failed");
		}

		//窗口创建成功,则显示并更新窗口
		ShowWindow(m_handler, SW_SHOWDEFAULT);
		UpdateWindow(m_handler);

	}  
public:
	int Run() {
		//消息循环
		//定义消息结构体
		MSG msg = { 0 };
		while (msg.message != WM_QUIT) {
			if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
				TranslateMessage(&msg);	//键盘按键转换，将虚拟键消息转换为字符消息
				DispatchMessage(&msg);	//把消息分派给相应的窗口过程
			} else {
				OnRender();
			}
		}
		//如果GetMessage函数不等于0，说明没有接受到WM_QUIT
		//for (bool bRet = 0; (bRet = GetMessage(&msg, 0, 0, 0)) != 0;) {
		//	//如果等于-1，说明GetMessage函数出错了，弹出错误框
		//	if (bRet == -1) {
		//		MessageBox(0, L"GetMessage Failed", L"Errow", MB_OK);
		//	} else { //如果等于其他值，说明接收到了消息
		//		TranslateMessage(&msg);	//键盘按键转换，将虚拟键消息转换为字符消息
		//		DispatchMessage(&msg);	//把消息分派给相应的窗口过程
		//	}
		//}

		return (int)msg.wParam;
	}

protected:
	static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		DirectX12 *callback = reinterpret_cast<DirectX12 *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

		//消息处理
		switch (msg) {
			//当窗口被销毁时，终止消息循环
		case WM_DESTROY:
			PostQuitMessage(0);	//终止消息循环，并发出WM_QUIT消息
			return 0;
		case WM_KEYDOWN: break;
		case WM_CREATE:
		{
			LPCREATESTRUCT pCreateStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
			SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreateStruct->lpCreateParams));
		}
		case WM_KEYUP: break;
		case WM_PAINT:
		{
			if (callback->is_available) {				callback->OnUpdate();
				callback->OnRender();
			}
		}
		break;
		default:
			break;
		}
		//将上面没有处理的消息转发给默认的窗口过程
		return DefWindowProc(hwnd, msg, wParam, lParam);
	}

public:
	DirectX12(int width, int height, LPCWSTR name) : m_width{ width }, m_height{ height },
		m_viewport{ 0.f, 0.f, static_cast<float>(width), static_cast<float>(height) },
		m_scissorRect{ 0, 0, static_cast<float>(width), static_cast<float>(height) },
		m_frameIndex{0},
		is_available{false}
{

		createWindow(width, height, name);

		UINT dxgiFactoryFlags = 0;

#pragma region Setup debug layer
		ComPtr<ID3D12Debug> debugController;
		if (!SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
			throw std::logic_error("cann't get debug interface");
		}
		debugController->EnableDebugLayer();
		dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#pragma endregion

#pragma region Create factory
	ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&dxgiFactory)));
#pragma endregion

#pragma region Create Device

	ComPtr<IDXGIAdapter1> adapter;

	for (UINT adapterIndex = 0; dxgiFactory->EnumAdapters1(adapterIndex, &adapter) != DXGI_ERROR_NOT_FOUND; ++adapterIndex) {
		DXGI_ADAPTER_DESC1 desc{};
		adapter->GetDesc1(&desc);

		std::cout << " - device[" << adapterIndex << "]: " << std::endl;
		std::wcout << "   \tDescription: " << desc.Description << std::endl;
		std::cout << "   \tDevice Id: " << desc.DeviceId << std::endl;
		std::cout << "   \tVendorId: " << desc.VendorId << std::endl;
		if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
			m_bestAdapter = adapter;
		}
	}

	if (m_bestAdapter == nullptr) {
		std::cerr << "could not find suitable adapter! use the first one!" << std::endl;
		dxgiFactory->EnumAdapters1(0, &m_bestAdapter);
	}
	{
		DXGI_ADAPTER_DESC1 desc{};
		m_bestAdapter->GetDesc1(&desc);
		std::wcout << "Select device: " << desc.Description << std::endl;
	}

	ThrowIfFailed(D3D12CreateDevice(m_bestAdapter.Get(), //此参数如果设置为nullptr，则使用主适配器
		D3D_FEATURE_LEVEL_12_0,		//应用程序需要硬件所支持的最低功能级别
		IID_PPV_ARGS(&d3dDevice)));	//返回所建设备
#pragma endregion

#pragma region Fetch something const vaules of descriptor
	m_rtvDescriptorSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	m_dsvDescriptorSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
#pragma endregion

#pragma region Check features supported
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msaaQualityLevels;
	msaaQualityLevels.Format = DXGI_FORMAT_R8G8B8A8_UNORM;	//UNORM是归一化处理的无符号整数
	msaaQualityLevels.SampleCount = 1;
	msaaQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	msaaQualityLevels.NumQualityLevels = 0;
	//当前图形驱动对MSAA多重采样的支持（注意：第二个参数即是输入又是输出）
	ThrowIfFailed(d3dDevice->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &msaaQualityLevels, sizeof(msaaQualityLevels)));
	//NumQualityLevels在Check函数里会进行设置
	//如果支持MSAA，则Check函数返回的NumQualityLevels > 0
	//expression为假（即为0），则终止程序运行，并打印一条出错信息
	assert(msaaQualityLevels.NumQualityLevels > 0);
#pragma endregion


#pragma region Command Object
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc = {};
	commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	commandQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

	ThrowIfFailed(d3dDevice->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&m_commandQueue)));
#pragma endregion

#pragma region Swap chain
	//ComPtr<IDXGISwapChain1> m_swapChain;
	//m_swapChain.Reset();
	//DXGI_SWAP_CHAIN_DESC swapChainDesc;	//交换链描述结构体
	//swapChainDesc.BufferDesc.Width = window.GetWidth();	//缓冲区分辨率的宽度
	//swapChainDesc.BufferDesc.Height = window.GetHeight();	//缓冲区分辨率的高度
	//swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;	//缓冲区的显示格式
	//swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;	//刷新率的分子
	//swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;	//刷新率的分母
	//swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;	//逐行扫描VS隔行扫描(未指定的)
	//swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;	//图像相对屏幕的拉伸（未指定的）
	//swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;	//将数据渲染至后台缓冲区（即作为渲染目标）
	//swapChainDesc.OutputWindow = m_windowHandler;	//渲染窗口句柄
	//swapChainDesc.SampleDesc.Count = 1;	//多重采样数量
	//swapChainDesc.SampleDesc.Quality = 0;	//多重采样质量
	//swapChainDesc.Windowed = true;	//是否窗口化
	//swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;	//固定写法
	//swapChainDesc.BufferCount = 2;	//后台缓冲区数量（双缓冲）
	//swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;	//自适应窗口模式（自动选择最适于当前窗口尺寸的显示模式）
	////利用DXGI接口下的工厂类创建交换链
	//ThrowIfFailed(dxgiFactory->CreateSwapChain(m_commandQueue.Get(), &swapChainDesc, m_swapChain.GetAddressOf()));

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	swapChainDesc.BufferCount = 2;
	swapChainDesc.Width = m_width;
	swapChainDesc.Height = m_height;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.SampleDesc.Count = 1;

	ComPtr<IDXGISwapChain1> swapChain1;
	ThrowIfFailed(dxgiFactory->CreateSwapChainForHwnd(m_commandQueue.Get(), m_handler, &swapChainDesc, nullptr, nullptr, &swapChain1));
	
	ThrowIfFailed(dxgiFactory->MakeWindowAssociation(m_handler, DXGI_MWA_NO_ALT_ENTER));

	ThrowIfFailed(swapChain1.As(&m_swapChain));
	int frameIdx = m_swapChain->GetCurrentBackBufferIndex();
#pragma endregion

	// framebuffer resource. color resource, depth resource, swapchain resource packed as a framebuffer in vulkan, here only have a swapchain resource(rtv)
#pragma region DescriptorHeap
	// RTV是SwapChain的Viewer，DSV是DepthResource的Viewer，实际存储数据的地方是SwapChain和DepthResource。

	/// ----------------------- 创建Swapchain资源，上面已完成 ------------------------

	/// -----------------------          创建Depth资源       ------------------------
	D3D12_RESOURCE_DESC dsvResourceDesc; //在CPU中创建好深度模板数据资源
	dsvResourceDesc.Alignment = 0;	//指定对齐
	dsvResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;	//指定资源维度（类型）为TEXTURE2D
	dsvResourceDesc.DepthOrArraySize = 1;	//纹理深度为1
	dsvResourceDesc.Width = m_width;	//资源宽
	dsvResourceDesc.Height = m_height;	//资源高
	dsvResourceDesc.MipLevels = 1;	//MIPMAP层级数量
	dsvResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;	//指定纹理布局（这里不指定）
	dsvResourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;	//深度模板资源的Flag
	dsvResourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;	//24位深度，8位模板,还有个无类型的格式DXGI_FORMAT_R24G8_TYPELESS也可以使用
	dsvResourceDesc.SampleDesc.Count = 4;	//多重采样数量
	dsvResourceDesc.SampleDesc.Quality = msaaQualityLevels.NumQualityLevels - 1;	//多重采样质量
	CD3DX12_CLEAR_VALUE optClear;	//清除资源的优化值，提高清除操作的执行速度（CreateCommittedResource函数中传入）
	optClear.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;//24位深度，8位模板,还有个无类型的格式DXGI_FORMAT_R24G8_TYPELESS也可以使用
	optClear.DepthStencil.Depth = 1;	//初始深度值为1
	optClear.DepthStencil.Stencil = 0;	//初始模板值为0
	//创建一个资源和一个堆，并将资源提交至堆中（将深度模板数据提交至GPU显存中）
	CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_DEFAULT);
	ThrowIfFailed(d3dDevice->CreateCommittedResource(&heapProperties,	//堆类型为默认堆（不能写入）
		D3D12_HEAP_FLAG_NONE,	//Flag
		&dsvResourceDesc,	//上面定义的DSV资源指针
		D3D12_RESOURCE_STATE_COMMON,	//资源的状态为初始状态
		&optClear,	//上面定义的优化值指针
		IID_PPV_ARGS(&m_depthStencilBuffer)));	//返回深度模板资源

	/// -----------------------       创建Swapchain的Viewer       ------------------------
	D3D12_DESCRIPTOR_HEAP_DESC rtvDescriptorHeapDesc;
	rtvDescriptorHeapDesc.NumDescriptors = swapChainDesc.BufferCount;
	rtvDescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvDescriptorHeapDesc.NodeMask = 0;

	ThrowIfFailed(d3dDevice->CreateDescriptorHeap(&rtvDescriptorHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

	// Create frame resources.
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

		// Create a RTV for each frame.
		m_renderTargets.resize((swapChainDesc.BufferCount));
		for (UINT n = 0; n < swapChainDesc.BufferCount; n++)
		{
			ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
			d3dDevice->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
			rtvHandle.Offset(1, m_rtvDescriptorSize);
		}
	}


	/// -----------------------       创建DepthStencil的Viewer       ------------------------
	D3D12_DESCRIPTOR_HEAP_DESC dsvDescriptorHeapDesc; //然后创建DSV堆
	dsvDescriptorHeapDesc.NumDescriptors = 1;
	dsvDescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvDescriptorHeapDesc.NodeMask = 0;
	ThrowIfFailed(d3dDevice->CreateDescriptorHeap(&dsvDescriptorHeapDesc, IID_PPV_ARGS(&m_dsvHeap)));

	//创建DSV(必须填充DSV属性结构体，和创建RTV不同，RTV是通过句柄)
	//D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
	//dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	//dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	//dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	//dsvDesc.Texture2D.MipSlice = 0;
	d3dDevice->CreateDepthStencilView(m_depthStencilBuffer.Get(),
		nullptr,	//D3D12_DEPTH_STENCIL_VIEW_DESC类型指针，可填&dsvDesc（见上注释代码），
		//由于在创建深度模板资源时已经定义深度模板数据属性，所以这里可以指定为空指针
		m_dsvHeap->GetCPUDescriptorHandleForHeapStart());	//DSV句柄

	/// ------------------------ DepthStencilBuffer的可见性转换 ------------------
	//m_commandList->ResourceBarrier(1,	//Barrier屏障个数
	//	&CD3DX12_RESOURCE_BARRIER::Transition(depthStencilBuffer.Get(),
	//		D3D12_RESOURCE_STATE_COMMON,	//转换前状态（创建时的状态，即CreateCommittedResource函数中定义的状态）
	//		D3D12_RESOURCE_STATE_DEPTH_WRITE));	//转换后状态为可写入的深度图，还有一个D3D12_RESOURCE_STATE_DEPTH_READ是只可读的深度图

#pragma endregion

	// uniform, sampler, shader constant, etc.
#pragma region Root Signature
	// Create an empty root signature.
	//ComPtr<ID3D12RootSignature> rootSignature;
	ComPtr<ID3D12RootSignature> &rootSignature = m_rootSignature;
	{
		constexpr bool useUniform = false;
		std::array<CD3DX12_ROOT_PARAMETER, useUniform ? 1 : 0> parameters;
		std::array<CD3DX12_STATIC_SAMPLER_DESC, 0> samplers;
		if constexpr (useUniform) {
			//根参数可以是描述符表、根描述符、根常量
			CD3DX12_ROOT_PARAMETER& uniformParameter = parameters[0];
			//创建由单个CBV所组成的描述符表
			CD3DX12_DESCRIPTOR_RANGE cbvTable;
			cbvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, //描述符类型
				1, //描述符数量
				0);//描述符所绑定的寄存器槽号
			uniformParameter.InitAsDescriptorTable(1, &cbvTable);
		}

		CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init(parameters.size(), parameters.data(), samplers.size(), samplers.data(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ComPtr<ID3DBlob> signature;
		ComPtr<ID3DBlob> error;
		ThrowIfFailed(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
		ThrowIfFailed(d3dDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootSignature)));
	}

#pragma endregion

#pragma region Pipeline state & load assets
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

		ThrowIfFailed(D3DCompileFromFile(L"shaders.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, nullptr));
		ThrowIfFailed(D3DCompileFromFile(L"shaders.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, nullptr));

		// Define the vertex input layout.
		D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
		};

		// Describe and create the graphics pipeline state object (PSO).
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
		psoDesc.pRootSignature = rootSignature.Get();
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
		psoDesc.SampleDesc.Count = 1; // MSAA采样数量，这里设置为1个sample

		ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState)));
	}

#pragma endregion


#pragma region Command list
	ThrowIfFailed(d3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));

	// Create the command list.
	ThrowIfFailed(d3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), pipelineState.Get(), IID_PPV_ARGS(&m_commandList)));

	// Command lists are created in the recording state, but there is nothing
	// to record yet. The main loop expects it to be closed, so close it now.
	ThrowIfFailed(m_commandList->Close());

#pragma endregion

#pragma region Vertex buffer
	// Create the vertex buffer.
	{
		// Define the geometry for a triangle.
		float m_aspectRatio = (float)m_width / m_height;
		std::vector<Vertex> triangleVertices{
			{ -1.0f, -1.0f, -1.0f, 1.f, 1.f, 1.f },
			{ -1.0f, +1.0f, -1.0f, 0.f, 0.f, 0.f },
			{ +1.0f, +1.0f, -1.0f, 1.f, 0.f, 0.f },
			{ +1.0f, -1.0f, -1.0f, 0.f, 1.f, 0.f },
			{ -1.0f, -1.0f, +1.0f, 0.f, 0.f, 1.f },
			{ -1.0f, +1.0f, +1.0f, 1.f, 1.f, 0.f },
			{ +1.0f, +1.0f, +1.0f, 0.f, 1.f, 1.f },
			{ +1.0f, -1.0f, +1.0f, 1.f, 0.f, 1.f }
		};
		//Vertex triangleVertices[] =
		//{
		//	{ { 0.0f, 0.25f * m_aspectRatio, 0.0f }, { 1.0f, 1.0f, 0.0f, 1.0f } },
		//	{ { 0.25f, -0.25f * m_aspectRatio, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
		//	{ { -0.25f, -0.25f * m_aspectRatio, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } }
		//};

		const UINT vertexBufferSize = sizeof(triangleVertices[0]) * triangleVertices.size();

		// Note: using upload heaps to transfer static data like vert buffers is not 
		// recommended. Every time the GPU needs it, the upload heap will be marshalled 
		// over. Please read up on Default Heap usage. An upload heap is used here for 
		// code simplicity and because there are very few verts to actually transfer.
		auto prop = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		auto buffer = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);
		d3dDevice->CreateCommittedResource(
			&prop,
			D3D12_HEAP_FLAG_NONE,
			&buffer,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_vertexBuffer));

		// Copy the triangle data to the vertex buffer.
		UINT8 *pVertexDataBegin;
		CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
		ThrowIfFailed(m_vertexBuffer->Map(0, &readRange, reinterpret_cast<void **>(&pVertexDataBegin)));
		memcpy(pVertexDataBegin, triangleVertices.data(), sizeof(triangleVertices));
		m_vertexBuffer->Unmap(0, nullptr);

		// Initialize the vertex buffer view.
		m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
		m_vertexBufferView.StrideInBytes = sizeof(Vertex);
		m_vertexBufferView.SizeInBytes = vertexBufferSize;

	}

#pragma endregion



#pragma region Create sync objects
	// Create synchronization objects and wait until assets have been uploaded to the GPU.
	{
		ThrowIfFailed(d3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
		m_fenceValue = 1;

		// Create an event handle to use for frame synchronization.
		m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (m_fenceEvent == nullptr)
		{
			ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
		}

		// Wait for the command list to execute; we are reusing the same command 
		// list in our main loop but for now, we just want to wait for setup to 
		// complete before continuing.
		WaitForPreviousFrame();
	}
#pragma endregion

	}

	void WaitForPreviousFrame() {
		// WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
		// This is code implemented as such for simplicity. The D3D12HelloFrameBuffering
		// sample illustrates how to use fences for efficient resource usage and to
		// maximize GPU utilization.

		// Signal and increment the m_fence value.
		const UINT64 fence = m_fenceValue;
		ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fence));
		m_fenceValue++;

		// Wait until the previous frame is finished.
		if (m_fence->GetCompletedValue() < fence)
		{
			ThrowIfFailed(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
			WaitForSingleObject(m_fenceEvent, INFINITE);
		}

		m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

		is_available = true;
	}

	void PopulateCommandList() {
		// Command list allocators can only be reset when the associated 
	// command lists have finished execution on the GPU; apps should use 
	// fences to determine GPU execution progress.
		ThrowIfFailed(m_commandAllocator->Reset());

		// However, when ExecuteCommandList() is called on a particular command 
		// list, that command list can then be reset at any time and must be before 
		// re-recording.
		ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), pipelineState.Get()));

		// Set necessary state.
		m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
		m_commandList->RSSetViewports(1, &m_viewport);
		m_commandList->RSSetScissorRects(1, &m_scissorRect);

		// Indicate that the back buffer will be used as a render target.
		auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
		m_commandList->ResourceBarrier(1, &barrier);

		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
		m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();

		// Record commands.
		const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
		m_commandList->ClearRenderTargetView(rtvHandle, clearColor, false, nullptr);
		m_commandList->ClearDepthStencilView(dsvHandle,	//DSV描述符句柄
			D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,	//FLAG
			1.0f,	//默认深度值
			0,	//默认模板值
			0,	//裁剪矩形数量
			nullptr);	//裁剪矩形指针
		m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
		m_commandList->DrawInstanced(3, 1, 0, 0);

		// Indicate that the back buffer will now be used to present.
		barrier = CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
		m_commandList->ResourceBarrier(1, &barrier);

		ThrowIfFailed(m_commandList->Close());
	}

	void OnUpdate() {
		std::cout << "on update ==========" << std::endl;
	}
	void OnRender() {
		// Record all the commands we need to render the scene into the command list.
		PopulateCommandList();

		// Execute the command list.
		ID3D12CommandList *ppCommandLists[] = { m_commandList.Get() };
		m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

		// Present the frame.
		ThrowIfFailed(m_swapChain->Present(1, 0));

		WaitForPreviousFrame();
	}


protected:
	ComPtr<IDXGIFactory4> dxgiFactory;
	ComPtr<ID3D12Device> d3dDevice;
	UINT64 m_fenceValue;
	HANDLE m_fenceEvent;
	ComPtr<ID3D12CommandQueue> m_commandQueue;
	ComPtr<ID3D12GraphicsCommandList> m_commandList;
	ComPtr<ID3D12CommandAllocator> m_commandAllocator;
	ComPtr<ID3D12PipelineState> pipelineState;
	ComPtr<IDXGIAdapter1> m_bestAdapter;
	std::vector< ComPtr<ID3D12Resource> > m_renderTargets;

	ComPtr<ID3D12RootSignature> m_rootSignature;
	ComPtr<IDXGISwapChain3> m_swapChain;
	ComPtr<ID3D12Fence> m_fence;
	ComPtr<ID3D12Resource> m_vertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
	ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
	ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
	ComPtr<ID3D12Resource> m_depthStencilBuffer;


	CD3DX12_VIEWPORT m_viewport;
	CD3DX12_RECT m_scissorRect;

	UINT m_frameIndex;
	UINT m_rtvDescriptorSize;
	UINT m_dsvDescriptorSize;

	bool is_available;

protected:
	HWND m_handler;
	int m_width, m_height;
};



int main() {
	DirectX12 dx{ 1280, 720, L"hello world" };

	dx.Run();
	return 0;
}
