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


//AnsiToWString������ת���ɿ��ַ����͵��ַ�����wstring��
//��Windowsƽ̨�ϣ�����Ӧ�ö�ʹ��wstring��wchar_t������ʽ�����ַ���ǰ+L
inline std::wstring AnsiToWString(const std::string &str) {
	WCHAR buffer[512];
	MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, buffer, 512);
	return std::wstring(buffer);
}

//DxException��
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


//�궨��ThrowIfFailed
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
		wc.style = CS_HREDRAW | CS_VREDRAW;	//����������߸ı䣬�����»��ƴ���
		wc.lpfnWndProc = wndProc;	//ָ�����ڹ���
		wc.cbClsExtra = 0;	//�����������ֶ���Ϊ��ǰӦ�÷��������ڴ�ռ䣨���ﲻ���䣬������0��
		wc.cbWndExtra = 0;	//�����������ֶ���Ϊ��ǰӦ�÷��������ڴ�ռ䣨���ﲻ���䣬������0��
		wc.hInstance = GetModuleHandle(0);	//Ӧ�ó���ʵ���������WinMain���룩
		wc.hIcon = LoadIcon(0, IDI_APPLICATION);	//ʹ��Ĭ�ϵ�Ӧ�ó���ͼ��
		wc.hCursor = LoadCursor(0, IDC_ARROW);	//ʹ�ñ�׼�����ָ����ʽ
		wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
		wc.lpszMenuName = 0;	//û�в˵���
		wc.lpszClassName = name;	//������
		wc.hIconSm = LoadIcon(0, IDI_APPLICATION);
		//������ע��ʧ��
		if (!RegisterClassEx(&wc)) {
			//��Ϣ����������1����Ϣ���������ھ������ΪNULL������2����Ϣ����ʾ���ı���Ϣ������3�������ı�������4����Ϣ����ʽ
			throw std::logic_error("RegisterClass Failed");
		}

		//������ע��ɹ�
		RECT R;	//�ü�����
		R.left = 0;
		R.top = 0;
		R.right = width;
		R.bottom = height;
		AdjustWindowRect(&R, WS_OVERLAPPEDWINDOW, false);	//���ݴ��ڵĿͻ�����С���㴰�ڵĴ�С

		//��������,���ز���ֵ
		m_handler = CreateWindow(wc.lpszClassName, name, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, width, height, 0, 0, GetModuleHandle(0), this);
		//���ڴ���ʧ��
		if (!m_handler) {
			throw std::logic_error("CreatWindow Failed");
		}

		//���ڴ����ɹ�,����ʾ�����´���
		ShowWindow(m_handler, SW_SHOWDEFAULT);
		UpdateWindow(m_handler);

	}  
public:
	int Run() {
		//��Ϣѭ��
		//������Ϣ�ṹ��
		MSG msg = { 0 };
		while (msg.message != WM_QUIT) {
			if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
				TranslateMessage(&msg);	//���̰���ת�������������Ϣת��Ϊ�ַ���Ϣ
				DispatchMessage(&msg);	//����Ϣ���ɸ���Ӧ�Ĵ��ڹ���
			} else {
				OnRender();
			}
		}
		//���GetMessage����������0��˵��û�н��ܵ�WM_QUIT
		//for (bool bRet = 0; (bRet = GetMessage(&msg, 0, 0, 0)) != 0;) {
		//	//�������-1��˵��GetMessage���������ˣ����������
		//	if (bRet == -1) {
		//		MessageBox(0, L"GetMessage Failed", L"Errow", MB_OK);
		//	} else { //�����������ֵ��˵�����յ�����Ϣ
		//		TranslateMessage(&msg);	//���̰���ת�������������Ϣת��Ϊ�ַ���Ϣ
		//		DispatchMessage(&msg);	//����Ϣ���ɸ���Ӧ�Ĵ��ڹ���
		//	}
		//}

		return (int)msg.wParam;
	}

protected:
	static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		DirectX12 *callback = reinterpret_cast<DirectX12 *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

		//��Ϣ����
		switch (msg) {
			//�����ڱ�����ʱ����ֹ��Ϣѭ��
		case WM_DESTROY:
			PostQuitMessage(0);	//��ֹ��Ϣѭ����������WM_QUIT��Ϣ
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
		//������û�д������Ϣת����Ĭ�ϵĴ��ڹ���
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

	ThrowIfFailed(D3D12CreateDevice(m_bestAdapter.Get(), //�˲����������Ϊnullptr����ʹ����������
		D3D_FEATURE_LEVEL_12_0,		//Ӧ�ó�����ҪӲ����֧�ֵ���͹��ܼ���
		IID_PPV_ARGS(&d3dDevice)));	//���������豸
#pragma endregion

#pragma region Fetch something const vaules of descriptor
	m_rtvDescriptorSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	m_dsvDescriptorSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
#pragma endregion

#pragma region Check features supported
	D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msaaQualityLevels;
	msaaQualityLevels.Format = DXGI_FORMAT_R8G8B8A8_UNORM;	//UNORM�ǹ�һ��������޷�������
	msaaQualityLevels.SampleCount = 1;
	msaaQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
	msaaQualityLevels.NumQualityLevels = 0;
	//��ǰͼ��������MSAA���ز�����֧�֣�ע�⣺�ڶ������������������������
	ThrowIfFailed(d3dDevice->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &msaaQualityLevels, sizeof(msaaQualityLevels)));
	//NumQualityLevels��Check��������������
	//���֧��MSAA����Check�������ص�NumQualityLevels > 0
	//expressionΪ�٣���Ϊ0��������ֹ�������У�����ӡһ��������Ϣ
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
	//DXGI_SWAP_CHAIN_DESC swapChainDesc;	//�����������ṹ��
	//swapChainDesc.BufferDesc.Width = window.GetWidth();	//�������ֱ��ʵĿ��
	//swapChainDesc.BufferDesc.Height = window.GetHeight();	//�������ֱ��ʵĸ߶�
	//swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;	//����������ʾ��ʽ
	//swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;	//ˢ���ʵķ���
	//swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;	//ˢ���ʵķ�ĸ
	//swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;	//����ɨ��VS����ɨ��(δָ����)
	//swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;	//ͼ�������Ļ�����죨δָ���ģ�
	//swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;	//��������Ⱦ����̨������������Ϊ��ȾĿ�꣩
	//swapChainDesc.OutputWindow = m_windowHandler;	//��Ⱦ���ھ��
	//swapChainDesc.SampleDesc.Count = 1;	//���ز�������
	//swapChainDesc.SampleDesc.Quality = 0;	//���ز�������
	//swapChainDesc.Windowed = true;	//�Ƿ񴰿ڻ�
	//swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;	//�̶�д��
	//swapChainDesc.BufferCount = 2;	//��̨������������˫���壩
	//swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;	//����Ӧ����ģʽ���Զ�ѡ�������ڵ�ǰ���ڳߴ����ʾģʽ��
	////����DXGI�ӿ��µĹ����ഴ��������
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
	// RTV��SwapChain��Viewer��DSV��DepthResource��Viewer��ʵ�ʴ洢���ݵĵط���SwapChain��DepthResource��

	/// ----------------------- ����Swapchain��Դ����������� ------------------------

	/// -----------------------          ����Depth��Դ       ------------------------
	D3D12_RESOURCE_DESC dsvResourceDesc; //��CPU�д��������ģ��������Դ
	dsvResourceDesc.Alignment = 0;	//ָ������
	dsvResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;	//ָ����Դά�ȣ����ͣ�ΪTEXTURE2D
	dsvResourceDesc.DepthOrArraySize = 1;	//�������Ϊ1
	dsvResourceDesc.Width = m_width;	//��Դ��
	dsvResourceDesc.Height = m_height;	//��Դ��
	dsvResourceDesc.MipLevels = 1;	//MIPMAP�㼶����
	dsvResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;	//ָ�������֣����ﲻָ����
	dsvResourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;	//���ģ����Դ��Flag
	dsvResourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;	//24λ��ȣ�8λģ��,���и������͵ĸ�ʽDXGI_FORMAT_R24G8_TYPELESSҲ����ʹ��
	dsvResourceDesc.SampleDesc.Count = 4;	//���ز�������
	dsvResourceDesc.SampleDesc.Quality = msaaQualityLevels.NumQualityLevels - 1;	//���ز�������
	CD3DX12_CLEAR_VALUE optClear;	//�����Դ���Ż�ֵ��������������ִ���ٶȣ�CreateCommittedResource�����д��룩
	optClear.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;//24λ��ȣ�8λģ��,���и������͵ĸ�ʽDXGI_FORMAT_R24G8_TYPELESSҲ����ʹ��
	optClear.DepthStencil.Depth = 1;	//��ʼ���ֵΪ1
	optClear.DepthStencil.Stencil = 0;	//��ʼģ��ֵΪ0
	//����һ����Դ��һ���ѣ�������Դ�ύ�����У������ģ�������ύ��GPU�Դ��У�
	CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_DEFAULT);
	ThrowIfFailed(d3dDevice->CreateCommittedResource(&heapProperties,	//������ΪĬ�϶ѣ�����д�룩
		D3D12_HEAP_FLAG_NONE,	//Flag
		&dsvResourceDesc,	//���涨���DSV��Դָ��
		D3D12_RESOURCE_STATE_COMMON,	//��Դ��״̬Ϊ��ʼ״̬
		&optClear,	//���涨����Ż�ֵָ��
		IID_PPV_ARGS(&m_depthStencilBuffer)));	//�������ģ����Դ

	/// -----------------------       ����Swapchain��Viewer       ------------------------
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


	/// -----------------------       ����DepthStencil��Viewer       ------------------------
	D3D12_DESCRIPTOR_HEAP_DESC dsvDescriptorHeapDesc; //Ȼ�󴴽�DSV��
	dsvDescriptorHeapDesc.NumDescriptors = 1;
	dsvDescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvDescriptorHeapDesc.NodeMask = 0;
	ThrowIfFailed(d3dDevice->CreateDescriptorHeap(&dsvDescriptorHeapDesc, IID_PPV_ARGS(&m_dsvHeap)));

	//����DSV(�������DSV���Խṹ�壬�ʹ���RTV��ͬ��RTV��ͨ�����)
	//D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
	//dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	//dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	//dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	//dsvDesc.Texture2D.MipSlice = 0;
	d3dDevice->CreateDepthStencilView(m_depthStencilBuffer.Get(),
		nullptr,	//D3D12_DEPTH_STENCIL_VIEW_DESC����ָ�룬����&dsvDesc������ע�ʹ��룩��
		//�����ڴ������ģ����Դʱ�Ѿ��������ģ���������ԣ������������ָ��Ϊ��ָ��
		m_dsvHeap->GetCPUDescriptorHandleForHeapStart());	//DSV���

	/// ------------------------ DepthStencilBuffer�Ŀɼ���ת�� ------------------
	//m_commandList->ResourceBarrier(1,	//Barrier���ϸ���
	//	&CD3DX12_RESOURCE_BARRIER::Transition(depthStencilBuffer.Get(),
	//		D3D12_RESOURCE_STATE_COMMON,	//ת��ǰ״̬������ʱ��״̬����CreateCommittedResource�����ж����״̬��
	//		D3D12_RESOURCE_STATE_DEPTH_WRITE));	//ת����״̬Ϊ��д������ͼ������һ��D3D12_RESOURCE_STATE_DEPTH_READ��ֻ�ɶ������ͼ

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
			//������������������������������������
			CD3DX12_ROOT_PARAMETER& uniformParameter = parameters[0];
			//�����ɵ���CBV����ɵ���������
			CD3DX12_DESCRIPTOR_RANGE cbvTable;
			cbvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, //����������
				1, //����������
				0);//���������󶨵ļĴ����ۺ�
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
		psoDesc.SampleDesc.Count = 1; // MSAA������������������Ϊ1��sample

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
		m_commandList->ClearDepthStencilView(dsvHandle,	//DSV���������
			D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,	//FLAG
			1.0f,	//Ĭ�����ֵ
			0,	//Ĭ��ģ��ֵ
			0,	//�ü���������
			nullptr);	//�ü�����ָ��
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
