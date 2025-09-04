#include "stdafx.h"
#include "ExamplePlugin.h" // CreateInstance, DeleteInstance を使うためにインクルード

// グローバルなプラグインインスタンスを作成・削除するための外部関数宣言
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
        // DLLが読み込まれた時にプラグインのインスタンスを作成します。
        CreateInstance(hModule);
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    case DLL_PROCESS_DETACH:
        // DLLが解放される時にプラグインのインスタンスを削除します。
        DeleteInstance();
        break;
    }
    return TRUE;
}