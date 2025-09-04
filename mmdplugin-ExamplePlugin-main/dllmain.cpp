#include "stdafx.h"
#include "ExamplePlugin.h" // CreateInstance, DeleteInstance ���g�����߂ɃC���N���[�h

// �O���[�o���ȃv���O�C���C���X�^���X���쐬�E�폜���邽�߂̊O���֐��錾
extern void CreateInstance(HMODULE hModule);
extern void DeleteInstance();

BOOL APIENTRY DllMain(HMODULE hModule,
    DWORD  ul_reason_for_call,
    LPVOID lpReserved
)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        // DLL���ǂݍ��܂ꂽ���Ƀv���O�C���̃C���X�^���X���쐬���܂��B
        CreateInstance(hModule);
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    case DLL_PROCESS_DETACH:
        // DLL���������鎞�Ƀv���O�C���̃C���X�^���X���폜���܂��B
        DeleteInstance();
        break;
    }
    return TRUE;
}