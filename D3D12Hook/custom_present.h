#pragma once
#include<dxgi.h>
#include<d3d12.h>
#ifdef __cplusplus
extern "C" {
#endif
//�Զ���Present�ĸ��Ӳ���
void CustomPresent(IDXGISwapChain*);
void CustomExecuteCommandLists(ID3D12CommandQueue*);
void CustomResizeBuffers(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
DWORD GetDLLPath(LPTSTR path, DWORD max_length);
#ifdef __cplusplus
}
#endif
