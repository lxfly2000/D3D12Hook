#include<Windows.h>
#include<dxgi1_5.h>
#include<wrl\client.h>
#include"..\minhook\include\MinHook.h"
#pragma comment(lib,"d3d12.lib")
#pragma comment(lib,"dxgi.lib")

#include"custom_present.h"

using namespace Microsoft::WRL;

typedef HRESULT(__stdcall* PFIDXGISwapChain_Present)(IDXGISwapChain*, UINT, UINT);
typedef void(__stdcall* PFID3D12CommandQueue_ExecuteCommandLists)(ID3D12CommandQueue*,UINT,ID3D12CommandList* const*);
static PFIDXGISwapChain_Present pfPresent = nullptr, pfOriginalPresent = nullptr;
static PFID3D12CommandQueue_ExecuteCommandLists pfExecuteCommandLists = nullptr, pfOriginalExecuteCommandLists = nullptr;
static HMODULE hDllModule;

DWORD GetDLLPath(LPTSTR path, DWORD max_length)
{
	return GetModuleFileName(hDllModule, path, max_length);
}

//Present��STDCALL���÷�ʽ��ֻ���THISָ����ڵ�һ��Ϳɰ��ǳ�Ա��������
HRESULT __stdcall HookedIDXGISwapChain_Present(IDXGISwapChain* p, UINT SyncInterval, UINT Flags)
{
	CustomPresent(p);
	//��ʱ���������أ�ֻ��ͨ��ָ����ã�����Ҫ�Ȱ�HOOK�رգ�����p->Present���ٿ���HOOK
	return pfOriginalPresent(p, SyncInterval, Flags);
}

//����Ŀǰû�з��ִӽ��������������еķ���
//https://stackoverflow.com/questions/36286425/how-do-i-get-the-directx-12-command-queue-from-a-swap-chain
void __stdcall HookedID3D12CommandQueue_ExecuteCommandLists(ID3D12CommandQueue* pCQ, UINT numLists, ID3D12CommandList* const* ppLists)
{
	CustomExecuteCommandLists(pCQ);
	return pfOriginalExecuteCommandLists(pCQ, numLists, ppLists);
}

BOOL GetPresentVAddr(PFIDXGISwapChain_Present*pfOutPresent,PFID3D12CommandQueue_ExecuteCommandLists*pfOutExecute)
{
	ComPtr<IDXGIFactory5>dxgiFactory;
	UINT dxgiFlags = 0;
#ifdef _DEBUG
	ComPtr<ID3D12Debug> debugController;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
	{
		debugController->EnableDebugLayer();
		dxgiFlags |= DXGI_CREATE_FACTORY_DEBUG;
	}
#endif
	if(FAILED(CreateDXGIFactory2(dxgiFlags,IID_PPV_ARGS(&dxgiFactory))))
		return FALSE;
	ComPtr<IDXGIAdapter1>dxgiAdapter;
	for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != dxgiFactory->EnumAdapters1(adapterIndex, dxgiAdapter.ReleaseAndGetAddressOf()); ++adapterIndex)
	{
		DXGI_ADAPTER_DESC1 desc = {};
		dxgiAdapter->GetDesc1(&desc);
		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
		{//�������������������
			continue;
		}
		//�����������D3D֧�ֵļ��ݼ�������ֱ��Ҫ��֧��12.1��������ע�ⷵ�ؽӿڵ��Ǹ���������Ϊ��nullptr������
		//�Ͳ���ʵ�ʴ���һ���豸�ˣ�Ҳ�������ǆ��µ��ٵ���release���ͷŽӿڡ���Ҳ��һ����Ҫ�ļ��ɣ����ס��
		if (SUCCEEDED(D3D12CreateDevice(dxgiAdapter.Get(), D3D_FEATURE_LEVEL_12_0, _uuidof(ID3D12Device), nullptr)))
		{
			break;
		}
	}
	ComPtr<ID3D12Device> pD3D12Device;
	//��һ���������Ϊ�յĻ����ܻᵼ�´����������ʧ��
	if(FAILED(D3D12CreateDevice(dxgiAdapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&pD3D12Device))))
		return FALSE;

	ID3D12CommandQueue* pCQ;
	D3D12_COMMAND_QUEUE_DESC cq_desc{};
	cq_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	cq_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	cq_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	cq_desc.NodeMask = 0;
	if (FAILED(pD3D12Device->CreateCommandQueue(&cq_desc, IID_PPV_ARGS(&pCQ))))
		return FALSE;
	//�鿴�ඨ���֪ExecuteCommandLists��VTable[10]��λ��
	*pfOutExecute = reinterpret_cast<PFID3D12CommandQueue_ExecuteCommandLists>(reinterpret_cast<INT_PTR*>(reinterpret_cast<INT_PTR*>(pCQ)[0])[10]);

	// Describe and create the swap chain.
	DXGI_SWAP_CHAIN_DESC1 sc_desc{};
	sc_desc.BufferCount = 3;//DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL,DXGI_SWAP_EFFECT_FLIP_DISCARDģʽ�»�����������Ϊ2
	sc_desc.Width = 800;
	sc_desc.Height = 600;
	sc_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sc_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	//https://docs.microsoft.com/zh-cn/windows/win32/direct3d12/porting-from-direct3d-11-to-direct3d-12#swapchains
	sc_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	sc_desc.SampleDesc.Count = 1;

	HWND hTemp;
	// Initialize the window class.
	WNDCLASSEX windowClass = { 0 };
	windowClass.cbSize = sizeof(WNDCLASSEX);
	windowClass.style = CS_HREDRAW | CS_VREDRAW;
	windowClass.lpfnWndProc = DefWindowProc;
	windowClass.hInstance = GetModuleHandle(NULL);
	windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
	windowClass.lpszClassName = L"DXSampleClass";
	RegisterClassEx(&windowClass);

	// Create the window and store a handle to it.
	hTemp = CreateWindow(windowClass.lpszClassName,TEXT("Window"),WS_OVERLAPPEDWINDOW,0,0,800,600,nullptr,nullptr,windowClass.hInstance,NULL);

	IDXGISwapChain1* pSC;
	if (FAILED(dxgiFactory->MakeWindowAssociation(hTemp, DXGI_MWA_NO_ALT_ENTER)))
		return FALSE;
	if(FAILED(dxgiFactory->CreateSwapChainForHwnd(pCQ,hTemp,&sc_desc,nullptr,nullptr,&pSC)))//����D3D12��һ������ӦΪֱ���������
		return FALSE;
	//��Ϊ��ͬ����麯���ǹ��õģ�����ֻ�贴��һ������Ķ���ͨ��ָ����ܻ�ȡ��������ַ
	//Present��VTable[8]��λ��
	*pfOutPresent = reinterpret_cast<PFIDXGISwapChain_Present>(reinterpret_cast<INT_PTR*>(reinterpret_cast<INT_PTR*>(pSC)[0])[8]);
	DestroyWindow(hTemp);
	pSC->Release();
	pCQ->Release();
	return TRUE;
}

//�����Է�����û��DllMainʱ����
extern "C" __declspec(dllexport) BOOL StartHook()
{
	if (!GetPresentVAddr(&pfPresent,&pfExecuteCommandLists))
		return FALSE;
	if (MH_Initialize() != MH_OK)
		return FALSE;
	if (MH_CreateHook(pfPresent, HookedIDXGISwapChain_Present, reinterpret_cast<void**>(&pfOriginalPresent)) != MH_OK)
		return FALSE;
	if (MH_CreateHook(pfExecuteCommandLists, HookedID3D12CommandQueue_ExecuteCommandLists, reinterpret_cast<void**>(&pfOriginalExecuteCommandLists)) != MH_OK)
		return FALSE;
	if (MH_EnableHook(pfPresent) != MH_OK)
		return FALSE;
	if (MH_EnableHook(pfExecuteCommandLists) != MH_OK)
		return FALSE;
	return TRUE;
}

//�����Է�����û��DllMainʱ����
extern "C" __declspec(dllexport) BOOL StopHook()
{
	if (MH_DisableHook(pfPresent) != MH_OK)
		return FALSE;
	if (MH_RemoveHook(pfPresent) != MH_OK)
		return FALSE;
	if (MH_Uninitialize() != MH_OK)
		return FALSE;
	return TRUE;
}

DWORD WINAPI TInitHook(LPVOID param)
{
	return StartHook();
}

BOOL WINAPI DllMain(HINSTANCE hInstDll, DWORD fdwReason, LPVOID lpvReserved)
{
	hDllModule = hInstDll;
	switch (fdwReason)
	{
	case DLL_PROCESS_ATTACH:
		DisableThreadLibraryCalls(hInstDll);
		CreateThread(NULL, 0, TInitHook, NULL, 0, NULL);
		break;
	case DLL_PROCESS_DETACH:
		StopHook();
		break;
	case DLL_THREAD_ATTACH:break;
	case DLL_THREAD_DETACH:break;
	}
	return TRUE;
}

//SetWindowHookEx��Ҫһ����������������DLL���ᱻ����
extern "C" __declspec(dllexport) LRESULT WINAPI HookProc(int code, WPARAM w, LPARAM l)
{
	return CallNextHookEx(NULL, code, w, l);
}