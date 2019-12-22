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

//Present是STDCALL调用方式，只需把THIS指针放在第一项就可按非成员函数调用
HRESULT __stdcall HookedIDXGISwapChain_Present(IDXGISwapChain* p, UINT SyncInterval, UINT Flags)
{
	CustomPresent(p);
	//此时函数被拦截，只能通过指针调用，否则要先把HOOK关闭，调用p->Present，再开启HOOK
	return pfOriginalPresent(p, SyncInterval, Flags);
}

//经查目前没有发现从交换链获得命令队列的方法
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
		{//软件虚拟适配器，跳过
			continue;
		}
		//检查适配器对D3D支持的兼容级别，这里直接要求支持12.1的能力，注意返回接口的那个参数被置为了nullptr，这样
		//就不会实际创建一个设备了，也不用我们啰嗦的再调用release来释放接口。这也是一个重要的技巧，请记住！
		if (SUCCEEDED(D3D12CreateDevice(dxgiAdapter.Get(), D3D_FEATURE_LEVEL_12_0, _uuidof(ID3D12Device), nullptr)))
		{
			break;
		}
	}
	ComPtr<ID3D12Device> pD3D12Device;
	//第一个参数如果为空的话可能会导致创建命令队列失败
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
	//查看类定义可知ExecuteCommandLists在VTable[10]的位置
	*pfOutExecute = reinterpret_cast<PFID3D12CommandQueue_ExecuteCommandLists>(reinterpret_cast<INT_PTR*>(reinterpret_cast<INT_PTR*>(pCQ)[0])[10]);

	// Describe and create the swap chain.
	DXGI_SWAP_CHAIN_DESC1 sc_desc{};
	sc_desc.BufferCount = 3;//DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL,DXGI_SWAP_EFFECT_FLIP_DISCARD模式下缓冲数量至少为2
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
	if(FAILED(dxgiFactory->CreateSwapChainForHwnd(pCQ,hTemp,&sc_desc,nullptr,nullptr,&pSC)))//对于D3D12第一个参数应为直接命令队列
		return FALSE;
	//因为相同类的虚函数是共用的，所以只需创建一个该类的对象，通过指针就能获取到函数地址
	//Present在VTable[8]的位置
	*pfOutPresent = reinterpret_cast<PFIDXGISwapChain_Present>(reinterpret_cast<INT_PTR*>(reinterpret_cast<INT_PTR*>(pSC)[0])[8]);
	DestroyWindow(hTemp);
	pSC->Release();
	pCQ->Release();
	return TRUE;
}

//导出以方便在没有DllMain时调用
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

//导出以方便在没有DllMain时调用
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

//SetWindowHookEx需要一个导出函数，否则DLL不会被加载
extern "C" __declspec(dllexport) LRESULT WINAPI HookProc(int code, WPARAM w, LPARAM l)
{
	return CallNextHookEx(NULL, code, w, l);
}