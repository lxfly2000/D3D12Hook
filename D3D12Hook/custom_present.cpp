#include"custom_present.h"
#include"d3dx12.h"
#include<dwrite.h>
#include<DirectXMath.h>
#include<map>
#include<string>
#include<ctime>
#include<wrl\client.h>
#include<d3d11on12.h>
#include<dxgi1_4.h>
#include<d2d1_3.h>

#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"dwrite.lib")
#pragma comment(lib,"d2d1.lib")

using namespace Microsoft::WRL;

#ifdef _DEBUG
#define C(x) if(FAILED(x)){MessageBox(NULL,TEXT(_CRT_STRINGIZE(x)),NULL,MB_ICONERROR);throw E_FAIL;}
#else
#define C(x) x
#endif

HRESULT CreateDWTextFormat(ComPtr<IDWriteTextFormat> & textformat,
	LPCWSTR fontName, DWRITE_FONT_WEIGHT fontWeight,
	FLOAT fontSize, DWRITE_FONT_STYLE fontStyle = DWRITE_FONT_STYLE_NORMAL,
	DWRITE_FONT_STRETCH fontExpand = DWRITE_FONT_STRETCH_NORMAL, LPCWSTR localeName = TEXT(""))
{
	ComPtr<IDWriteFactory> dwfactory;
	C(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(dwfactory), (IUnknown**)&dwfactory));
	C(dwfactory->CreateTextFormat(fontName, NULL, fontWeight, fontStyle, fontExpand, fontSize, localeName,
		textformat.ReleaseAndGetAddressOf()));
	return S_OK;
}

class D2DCustomPresent
{
private:
	unsigned t1, t2, fcount;
	std::wstring display_text;
	int current_fps;
	TCHAR time_text[32], fps_text[32];

	TCHAR font_name[256], font_size[16], text_x[16], text_y[16], text_align[16], text_valign[16], display_text_fmt[256], fps_fmt[32], time_fmt[32];
	TCHAR font_red[16], font_green[16], font_blue[16], font_alpha[16];
	TCHAR font_shadow_red[16], font_shadow_green[16], font_shadow_blue[16], font_shadow_alpha[16], font_shadow_distance[16];
	int font_weight, period_frames;
	D2D1_RECT_F rText, rTextShadow;
	ComPtr<ID2D1SolidColorBrush> calcColor, calcShadowColor;
	ComPtr<IDWriteTextFormat> textFormat;
	ComPtr<ID3D11On12Device> m_d3d11On12Device;
	ComPtr<ID3D11DeviceContext>m_d3d11DeviceContext;
	ComPtr<ID2D1Device2> m_d2dDevice;
	ComPtr<ID2D1DeviceContext2> m_d2dDeviceContext;
	ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
	std::vector<ComPtr<ID3D11Resource>> m_wrappedBackBuffers;
	std::vector<ComPtr<ID3D12Resource>> m_renderTargets;
	std::vector<ComPtr<ID2D1Bitmap1>> m_d2dRenderTargets;
	std::vector<ComPtr<ID3D12CommandAllocator>> m_commandAllocators;
	// App resources.
	UINT m_rtvDescriptorSize;
	// Synchronization objects.
	UINT m_frameIndex;

	IDXGISwapChain3* m_swapChain;
public:
	D2DCustomPresent() :t1(0), t2(0), fcount(0),m_frameIndex(0)
	{
	}
	~D2DCustomPresent()
	{
		Uninit();
	}
	BOOL Init(IDXGISwapChain* pSC,ID3D12CommandQueue*pCQ)
	{
		C(pSC->QueryInterface(IID_PPV_ARGS(&m_swapChain)));
		TCHAR szConfPath[MAX_PATH];
		GetDLLPath(szConfPath, ARRAYSIZE(szConfPath));
		lstrcpy(wcsrchr(szConfPath, '.'), TEXT(".ini"));
#define GetInitConfStr(key,def) GetPrivateProfileString(TEXT("Init"),TEXT(_STRINGIZE(key)),def,key,ARRAYSIZE(key),szConfPath)
#define GetInitConfInt(key,def) key=GetPrivateProfileInt(TEXT("Init"),TEXT(_STRINGIZE(key)),def,szConfPath)
#define F(_i_str) (float)_wtof(_i_str)
		GetInitConfStr(font_name, TEXT("宋体"));
		GetInitConfStr(font_size, TEXT("48"));
		GetInitConfStr(font_red, TEXT("1"));
		GetInitConfStr(font_green, TEXT("1"));
		GetInitConfStr(font_blue, TEXT("0"));
		GetInitConfStr(font_alpha, TEXT("1"));
		GetInitConfStr(font_shadow_red, TEXT("0.5"));
		GetInitConfStr(font_shadow_green, TEXT("0.5"));
		GetInitConfStr(font_shadow_blue, TEXT("0"));
		GetInitConfStr(font_shadow_alpha, TEXT("1"));
		GetInitConfStr(font_shadow_distance, TEXT("2"));
		GetInitConfInt(font_weight, 400);
		GetInitConfStr(text_x, TEXT("0"));
		GetInitConfStr(text_y, TEXT("0"));
		GetInitConfStr(text_align, TEXT("left"));
		GetInitConfStr(text_valign, TEXT("top"));
		GetInitConfInt(period_frames, 60);
		GetInitConfStr(time_fmt, TEXT("%H:%M:%S"));
		GetInitConfStr(fps_fmt, TEXT("FPS:%3d"));
		GetInitConfStr(display_text_fmt, TEXT("{fps}"));

		//https://docs.microsoft.com/zh-cn/windows/win32/direct3d12/d2d-using-d3d11on12
		//https://github.com/microsoft/DirectX-Graphics-Samples/blob/master/Samples/Desktop/D3D1211On12/src/D3D1211On12.cpp
		UINT dxgiFactoryFlags = 0;
		UINT d3d11DeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
		D2D1_FACTORY_OPTIONS d2dFactoryOptions{};
#if defined(_DEBUG)
		// Enable the debug layer (requires the Graphics Tools "optional feature").
		// NOTE: Enabling the debug layer after device creation will invalidate the active device.
		{
			ComPtr<ID3D12Debug> debugController;
			if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
			{
				debugController->EnableDebugLayer();

				// Enable additional debug layers.
				dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
				d3d11DeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
				d2dFactoryOptions.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
			}
		}
#endif
		ComPtr<IDXGIFactory2> factory;
		C(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));
		ID3D12Device* pD3D12Device;
		C(pSC->GetDevice(IID_PPV_ARGS(&pD3D12Device)));
		ComPtr<ID3D11Device> d3d11Device;
		C(D3D11On12CreateDevice(pD3D12Device, d3d11DeviceFlags,nullptr,0,reinterpret_cast<IUnknown**>(&pCQ),1,0,&d3d11Device,&m_d3d11DeviceContext,nullptr));
		C(d3d11Device.As(&m_d3d11On12Device));

		ComPtr<ID2D1Factory3> d2dFactory;
		C(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory3), &d2dFactoryOptions, &d2dFactory));
		ComPtr<IDXGIDevice> dxgiDevice;
		C(m_d3d11On12Device.As(&dxgiDevice));
		C(d2dFactory->CreateDevice(dxgiDevice.Get(), &m_d2dDevice));
		D2D1_DEVICE_CONTEXT_OPTIONS deviceOptions = D2D1_DEVICE_CONTEXT_OPTIONS_NONE;
		C(m_d2dDevice->CreateDeviceContext(deviceOptions, &m_d2dDeviceContext));
		// Query the desktop's dpi settings, which will be used to create
		// D2D's render targets.
		float dpiX;
		float dpiY;
		d2dFactory->GetDesktopDpi(&dpiX, &dpiY);
		D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
			D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED),dpiX,dpiY);
		DXGI_SWAP_CHAIN_DESC sc_desc;
		C(pSC->GetDesc(&sc_desc));
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
		rtvHeapDesc.NumDescriptors = sc_desc.BufferCount;
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		C(pD3D12Device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));
		m_rtvDescriptorSize = pD3D12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		m_wrappedBackBuffers.resize(sc_desc.BufferCount);
		m_renderTargets.resize(sc_desc.BufferCount);
		m_d2dRenderTargets.resize(sc_desc.BufferCount);
		m_commandAllocators.resize(sc_desc.BufferCount);
		// Create frame resources.
		{
			CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

			// Create a RTV, D2D render target, and a command allocator for each frame.
			for (UINT n = 0; n < sc_desc.BufferCount; n++)
			{
				C(pSC->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
				pD3D12Device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);

				// Create a wrapped 11On12 resource of this back buffer. Since we are 
				// rendering all D3D12 content first and then all D2D content, we specify 
				// the In resource state as RENDER_TARGET - because D3D12 will have last 
				// used it in this state - and the Out resource state as PRESENT. When 
				// ReleaseWrappedResources() is called on the 11On12 device, the resource 
				// will be transitioned to the PRESENT state.
				D3D11_RESOURCE_FLAGS d3d11Flags = { D3D11_BIND_RENDER_TARGET };
				C(m_d3d11On12Device->CreateWrappedResource(m_renderTargets[n].Get(),&d3d11Flags,D3D12_RESOURCE_STATE_RENDER_TARGET,
					D3D12_RESOURCE_STATE_PRESENT,IID_PPV_ARGS(&m_wrappedBackBuffers[n])));

				// Create a render target for D2D to draw directly to this back buffer.
				ComPtr<IDXGISurface> surface;
				C(m_wrappedBackBuffers[n].As(&surface));
				C(m_d2dDeviceContext->CreateBitmapFromDxgiSurface(surface.Get(),&bitmapProperties,&m_d2dRenderTargets[n]));

				rtvHandle.Offset(1, m_rtvDescriptorSize);

				C(pD3D12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[n])));
			}
		}
		C(CreateDWTextFormat(textFormat, font_name, (DWRITE_FONT_WEIGHT)font_weight, F(font_size)));
		float fWidth = (float)sc_desc.BufferDesc.Width, fHeight = (float)sc_desc.BufferDesc.Height;

		if (lstrcmpi(text_align, TEXT("right")) == 0)
		{
			textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
			rText.left = 0;
			rText.right = F(text_x) * fWidth;
		}
		else if (lstrcmpi(text_align, TEXT("center")) == 0)
		{
			textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
			if (F(text_x) > 0.5f)
			{
				rText.left = 0;
				rText.right = 2.0f * fWidth * F(text_x);
			}
			else
			{
				rText.left = 2.0f * fWidth * F(text_x) - fWidth;
				rText.right = fWidth;
			}
		}
		else
		{
			textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
			rText.left = F(text_x) * fWidth;
			rText.right = fWidth;
		}
		if (lstrcmpi(text_valign, TEXT("bottom")) == 0)
		{
			textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_FAR);
			rText.top = 0;
			rText.bottom = F(text_y) * fHeight;
		}
		else if (lstrcmpi(text_valign, TEXT("center")) == 0)
		{
			textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
			if (F(text_y) > 0.5f)
			{
				rText.top = 0;
				rText.bottom = 2.0f * fHeight * F(text_y);
			}
			else
			{
				rText.top = 2.0f * fHeight * F(text_y) - fHeight;
				rText.bottom = fHeight;
			}
		}
		else
		{
			textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
			rText.top = F(text_y) * fHeight;
			rText.bottom = fHeight;
		}
		rTextShadow.left = rText.left + F(font_shadow_distance);
		rTextShadow.top = rText.top + F(font_shadow_distance);
		rTextShadow.right = rText.right + F(font_shadow_distance);
		rTextShadow.bottom = rText.bottom + F(font_shadow_distance);
		C(m_d2dDeviceContext->CreateSolidColorBrush(D2D1::ColorF(F(font_shadow_red),F(font_shadow_green),F(font_shadow_blue),F(font_shadow_alpha)), &calcShadowColor));
		C(m_d2dDeviceContext->CreateSolidColorBrush(D2D1::ColorF(F(font_red),F(font_green),F(font_blue),F(font_alpha)), &calcColor));
		return TRUE;
	}
	void Uninit()
	{
	}
	void Draw()
	{
		if (fcount-- == 0)
		{
			fcount = period_frames;
			t1 = t2;
			t2 = GetTickCount();
			if (t1 == t2)
				t1--;
			current_fps = period_frames * 1000 / (t2 - t1);
			wsprintf(fps_text, fps_fmt, current_fps);//注意wsprintf不支持浮点数格式化
			time_t t1 = time(NULL);
			tm tm1;
			localtime_s(&tm1, &t1);
			wcsftime(time_text, ARRAYSIZE(time_text), time_fmt, &tm1);
			display_text = display_text_fmt;
			size_t pos = display_text.find(TEXT("\\n"));
			if (pos != std::wstring::npos)
				display_text.replace(pos, 2, TEXT("\n"));
			pos = display_text.find(TEXT("{fps}"));
			if (pos != std::wstring::npos)
				display_text.replace(pos, 5, fps_text);
			pos = display_text.find(TEXT("{time}"));
			if (pos != std::wstring::npos)
				display_text.replace(pos, 6, time_text);
		}
		m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
		// Acquire our wrapped render target resource for the current back buffer.
		m_d3d11On12Device->AcquireWrappedResources(m_wrappedBackBuffers[m_frameIndex].GetAddressOf(), 1);

		// Render text directly to the back buffer.
		m_d2dDeviceContext->SetTarget(m_d2dRenderTargets[m_frameIndex].Get());
		m_d2dDeviceContext->BeginDraw();
		m_d2dDeviceContext->SetTransform(D2D1::Matrix3x2F::Identity());
		m_d2dDeviceContext->DrawTextW(display_text.c_str(), (UINT32)display_text.length(), textFormat.Get(), rTextShadow, calcShadowColor.Get());
		m_d2dDeviceContext->DrawTextW(display_text.c_str(), (UINT32)display_text.length(), textFormat.Get(), rText, calcColor.Get());
		C(m_d2dDeviceContext->EndDraw());

		// Release our wrapped render target resource. Releasing 
		// transitions the back buffer resource to the state specified
		// as the OutState when the wrapped resource was created.
		m_d3d11On12Device->ReleaseWrappedResources(m_wrappedBackBuffers[m_frameIndex].GetAddressOf(), 1);

		// Flush to submit the 11 command list to the shared command queue.
		m_d3d11DeviceContext->Flush();
	}
	void ExecuteExtraCommandList()
	{
		//Currently nothing to do.
	}
};

struct SCV
{
	IDXGISwapChain* pSC;
	ID3D12CommandQueue* pCQ;
	D2DCustomPresent* pPresent;
	BOOL NewPresent()
	{
		if (pPresent)
			delete pPresent;
		pPresent = new D2DCustomPresent();
		return pPresent->Init(pSC, pCQ);
	}
	SCV():pSC(nullptr),pCQ(nullptr),pPresent(nullptr)
	{
	}
	SCV(IDXGISwapChain* sc, ID3D12CommandQueue* cq):SCV()
	{
		pSC = sc;
		pCQ = cq;
	}
	SCV(SCV&& other)noexcept:SCV()
	{
		pSC = other.pSC;
		pCQ = other.pCQ;
		pPresent = other.pPresent;
		other.pPresent = nullptr;
	}
	~SCV()
	{
		if (pPresent)
		{
			delete pPresent;
			pPresent = nullptr;
		}
	}
};
static std::map<IDXGISwapChain*, ID3D12Device*> s_d;
static std::map<ID3D12CommandQueue*, ID3D12Device*> c_d;
static std::map<ID3D12Device*, SCV> d_sc;

SCV& GetSCVorNew(ID3D12Device* pDev,IDXGISwapChain*pSC,ID3D12CommandQueue*pCQ)
{
	if (d_sc.find(pDev) == d_sc.end())
		d_sc.insert(std::make_pair(pDev, SCV(pSC, pCQ)));
	SCV& scv = d_sc[pDev];
	if (pSC)
		scv.pSC = pSC;
	if (pCQ)
		scv.pCQ = pCQ;
	return scv;
}

void CustomPresent(IDXGISwapChain* pSC)
{
	//查找或记录设备
	ID3D12Device* pDev;
	if (FAILED(pSC->GetDevice(IID_PPV_ARGS(&pDev))))
		return;
	if (s_d.find(pSC) == s_d.end())
		s_d.insert(std::make_pair(pSC, pDev));
	//查找或记录SCV
	SCV& scv = GetSCVorNew(pDev,pSC,nullptr);
	if (scv.pCQ)
	{
		if (scv.pPresent)
			scv.pPresent->Draw();
		else
			scv.NewPresent();
	}
}

void CustomExecuteCommandLists(ID3D12CommandQueue*pCQ)
{
	//查找或记录设备
	ID3D12Device* pDev;
	if (FAILED(pCQ->GetDevice(IID_PPV_ARGS(&pDev))))
		return;
	if (c_d.find(pCQ) == c_d.end())
		c_d.insert(std::make_pair(pCQ, pDev));
	//查找或记录SCV
	SCV& scv = GetSCVorNew(pDev,nullptr,pCQ);
	if (scv.pSC)
	{
		if (scv.pPresent)
			scv.pPresent->ExecuteExtraCommandList();
		else
			scv.NewPresent();
	}
}