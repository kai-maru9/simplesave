#include "stdafx.h"
#include "ExamplePlugin.h"
#include <experimental/filesystem>
#include <shlwapi.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>
#include <fstream>

#pragma comment(lib, "shlwapi.lib")
namespace fs = std::experimental::filesystem;

// --- 設定管理 ---
struct PluginSettings {
    int intervalMinutes = 5;           // バックアップ間隔（分）
    bool showSuccessDialog = false;    // 成功ダイアログ表示
    int maxBackupFiles = 50;           // 最大バックアップ数
    bool autoBackupEnabled = true;     // 自動バックアップ有効/無効

    fs::path settingsPath;

    void Load(const fs::path& dllPath) {
        settingsPath = dllPath.parent_path() / L"AutoBackup.ini";

        // INIファイルが存在しない場合は作成
        if (!fs::exists(settingsPath)) {
            Save();
            CreateDefaultIni();
            return;
        }

        // 設定を読み込み
        intervalMinutes = GetPrivateProfileIntW(L"Settings", L"IntervalMinutes", 5, settingsPath.c_str());
        if (intervalMinutes < 1) intervalMinutes = 1;  // 最低1分
        if (intervalMinutes > 1440) intervalMinutes = 1440;  // 最大24時間

        showSuccessDialog = GetPrivateProfileIntW(L"Settings", L"ShowSuccessDialog", 0, settingsPath.c_str()) != 0;
        maxBackupFiles = GetPrivateProfileIntW(L"Settings", L"MaxBackupFiles", 50, settingsPath.c_str());
        if (maxBackupFiles < 1) maxBackupFiles = 1;

        autoBackupEnabled = GetPrivateProfileIntW(L"Settings", L"AutoBackupEnabled", 1, settingsPath.c_str()) != 0;
    }

    void Save() {
        // 設定を保存
        WritePrivateProfileStringW(L"Settings", L"IntervalMinutes", std::to_wstring(intervalMinutes).c_str(), settingsPath.c_str());
        WritePrivateProfileStringW(L"Settings", L"ShowSuccessDialog", showSuccessDialog ? L"1" : L"0", settingsPath.c_str());
        WritePrivateProfileStringW(L"Settings", L"MaxBackupFiles", std::to_wstring(maxBackupFiles).c_str(), settingsPath.c_str());
        WritePrivateProfileStringW(L"Settings", L"AutoBackupEnabled", autoBackupEnabled ? L"1" : L"0", settingsPath.c_str());
    }

    void CreateDefaultIni() {
        // デフォルトのINIファイルを作成（コメント付き）
        std::wofstream ofs(settingsPath);
        if (ofs.is_open()) {
            ofs << L"; AutoBackup Plugin Settings\n";
            ofs << L"; 設定を変更後、MMDを再起動すると反映されます\n";
            ofs << L";\n";
            ofs << L"; IntervalMinutes: バックアップ間隔（分）1-1440\n";
            ofs << L"; ShowSuccessDialog: 成功時のダイアログ表示 (0=非表示, 1=表示)\n";
            ofs << L"; MaxBackupFiles: 最大バックアップファイル数\n";
            ofs << L"; AutoBackupEnabled: 自動バックアップの有効/無効 (0=無効, 1=有効)\n";
            ofs << L"\n";
            ofs << L"[Settings]\n";
            ofs << L"IntervalMinutes=" << intervalMinutes << L"\n";
            ofs << L"ShowSuccessDialog=" << (showSuccessDialog ? 1 : 0) << L"\n";
            ofs << L"MaxBackupFiles=" << maxBackupFiles << L"\n";
            ofs << L"AutoBackupEnabled=" << (autoBackupEnabled ? 1 : 0) << L"\n";
            ofs.close();
        }
    }
};

static PluginSettings g_settings;

// --- グローバルなプラグインインスタンス管理 ---
static CPlugin* g_pPlugin = nullptr;
static LONG_PTR g_pOriginWndProc = NULL;

// メニューID
enum MenuCommands {
    ID_BACKUP_NOW = 41001,
    ID_TOGGLE_AUTO = 41002,
    ID_TOGGLE_DIALOG = 41003,
    ID_INTERVAL_1 = 41010,
    ID_INTERVAL_3 = 41011,
    ID_INTERVAL_5 = 41012,
    ID_INTERVAL_10 = 41013,
    ID_INTERVAL_15 = 41014,
    ID_INTERVAL_30 = 41015,
    ID_INTERVAL_60 = 41016,
    ID_MAX_FILES_10 = 41020,
    ID_MAX_FILES_30 = 41021,
    ID_MAX_FILES_50 = 41022,
    ID_MAX_FILES_100 = 41023,
    ID_MAX_FILES_UNLIMITED = 41024,
    ID_ABOUT = 41030
};

static LRESULT CALLBACK pluginWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (g_pPlugin) {
        if (uMsg == WM_COMMAND) {
            int cmd = LOWORD(wParam);

            switch (cmd) {
            case ID_BACKUP_NOW:
                g_pPlugin->triggerSave(true);  // 手動バックアップは常にダイアログ表示
                return 0;

            case ID_TOGGLE_AUTO:
                g_settings.autoBackupEnabled = !g_settings.autoBackupEnabled;
                g_settings.Save();
                {
                    std::wstring msg = g_settings.autoBackupEnabled ?
                        L"自動バックアップを有効にしました" :
                        L"自動バックアップを無効にしました";
                    MessageBoxW(hWnd, msg.c_str(), L"自動バックアップ", MB_OK | MB_ICONINFORMATION);
                }
                g_pPlugin->updateMenu();
                return 0;

            case ID_TOGGLE_DIALOG:
                g_settings.showSuccessDialog = !g_settings.showSuccessDialog;
                g_settings.Save();
                g_pPlugin->updateMenu();
                return 0;

                // 間隔設定
            case ID_INTERVAL_1:
            case ID_INTERVAL_3:
            case ID_INTERVAL_5:
            case ID_INTERVAL_10:
            case ID_INTERVAL_15:
            case ID_INTERVAL_30:
            case ID_INTERVAL_60:
            {
                int intervals[] = { 1, 3, 5, 10, 15, 30, 60 };
                g_settings.intervalMinutes = intervals[cmd - ID_INTERVAL_1];
                g_settings.Save();
                g_pPlugin->updateMenu();
                MessageBoxW(hWnd,
                    (L"バックアップ間隔を " + std::to_wstring(g_settings.intervalMinutes) + L" 分に設定しました").c_str(),
                    L"設定変更", MB_OK | MB_ICONINFORMATION);
            }
            return 0;

            // 最大ファイル数設定
            case ID_MAX_FILES_10:
            case ID_MAX_FILES_30:
            case ID_MAX_FILES_50:
            case ID_MAX_FILES_100:
            {
                int maxFiles[] = { 10, 30, 50, 100 };
                g_settings.maxBackupFiles = maxFiles[cmd - ID_MAX_FILES_10];
                g_settings.Save();
                g_pPlugin->updateMenu();
            }
            return 0;

            case ID_MAX_FILES_UNLIMITED:
                g_settings.maxBackupFiles = 9999;
                g_settings.Save();
                g_pPlugin->updateMenu();
                return 0;

            case ID_ABOUT:
            {
                wchar_t msg[512];
                swprintf_s(msg,
                    L"自動バックアップ プラグイン v1.0\n\n"
                    L"現在の設定:\n"
                    L"・バックアップ間隔: %d 分\n"
                    L"・最大バックアップ数: %d\n"
                    L"・成功ダイアログ: %s\n"
                    L"・自動バックアップ: %s\n\n"
                    L"設定ファイル: AutoBackup.ini",
                    g_settings.intervalMinutes,
                    g_settings.maxBackupFiles == 9999 ? -1 : g_settings.maxBackupFiles,
                    g_settings.showSuccessDialog ? L"表示" : L"非表示",
                    g_settings.autoBackupEnabled ? L"有効" : L"無効");
                MessageBoxW(hWnd, msg, L"About", MB_OK | MB_ICONINFORMATION);
            }
            return 0;
            }
        }
    }
    return CallWindowProc((WNDPROC)g_pOriginWndProc, hWnd, uMsg, wParam, lParam);
}

void CreateInstance(HMODULE hModule) {
    if (g_pPlugin == nullptr) {
        g_pPlugin = new CPlugin(hModule);

        // 設定を読み込み
        wchar_t dllPath[MAX_PATH];
        GetModuleFileNameW(hModule, dllPath, MAX_PATH);
        g_settings.Load(fs::path(dllPath));
    }
}

void DeleteInstance() {
    if (g_pPlugin) {
        delete g_pPlugin;
        g_pPlugin = nullptr;
    }
}

// ---------------------------------------------

CPlugin::CPlugin(HMODULE hModule) : m_hModule(hModule), m_isThreadRunning(false), m_hMenu(NULL) {}
CPlugin::~CPlugin() {}

void CPlugin::start() {
    createMenu();
    HWND hWnd = getHWND();
    g_pOriginWndProc = GetWindowLongPtr(hWnd, GWLP_WNDPROC);
    SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)pluginWndProc);
    m_isThreadRunning = true;
    m_thread = std::thread(&CPlugin::backupWorker, this);
}

void CPlugin::stop() {
    if (g_pOriginWndProc) {
        SetWindowLongPtr(getHWND(), GWLP_WNDPROC, g_pOriginWndProc);
        g_pOriginWndProc = NULL;
    }
    m_isThreadRunning = false;
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void CPlugin::createMenu() {
    HMENU menu = GetMenu(getHWND());
    HMENU newMenu = CreatePopupMenu();
    m_hMenu = newMenu;

    AppendMenuW(newMenu, MF_STRING, ID_BACKUP_NOW, L"今すぐバックアップ(&B)\tCtrl+Shift+S");
    AppendMenuW(newMenu, MF_SEPARATOR, 0, NULL);

    // 自動バックアップ ON/OFF
    AppendMenuW(newMenu, MF_STRING | (g_settings.autoBackupEnabled ? MF_CHECKED : 0),
        ID_TOGGLE_AUTO, L"自動バックアップ(&A)");

    // 成功ダイアログ表示 ON/OFF
    AppendMenuW(newMenu, MF_STRING | (g_settings.showSuccessDialog ? MF_CHECKED : 0),
        ID_TOGGLE_DIALOG, L"完了通知を表示(&N)");

    AppendMenuW(newMenu, MF_SEPARATOR, 0, NULL);

    // バックアップ間隔サブメニュー
    HMENU intervalMenu = CreatePopupMenu();
    AppendMenuW(intervalMenu, MF_STRING | (g_settings.intervalMinutes == 1 ? MF_CHECKED : 0), ID_INTERVAL_1, L"1分");
    AppendMenuW(intervalMenu, MF_STRING | (g_settings.intervalMinutes == 3 ? MF_CHECKED : 0), ID_INTERVAL_3, L"3分");
    AppendMenuW(intervalMenu, MF_STRING | (g_settings.intervalMinutes == 5 ? MF_CHECKED : 0), ID_INTERVAL_5, L"5分");
    AppendMenuW(intervalMenu, MF_STRING | (g_settings.intervalMinutes == 10 ? MF_CHECKED : 0), ID_INTERVAL_10, L"10分");
    AppendMenuW(intervalMenu, MF_STRING | (g_settings.intervalMinutes == 15 ? MF_CHECKED : 0), ID_INTERVAL_15, L"15分");
    AppendMenuW(intervalMenu, MF_STRING | (g_settings.intervalMinutes == 30 ? MF_CHECKED : 0), ID_INTERVAL_30, L"30分");
    AppendMenuW(intervalMenu, MF_STRING | (g_settings.intervalMinutes == 60 ? MF_CHECKED : 0), ID_INTERVAL_60, L"60分");
    AppendMenuW(newMenu, MF_POPUP, (UINT_PTR)intervalMenu, L"バックアップ間隔(&I) >");

    // 最大ファイル数サブメニュー
    HMENU maxFilesMenu = CreatePopupMenu();
    AppendMenuW(maxFilesMenu, MF_STRING | (g_settings.maxBackupFiles == 10 ? MF_CHECKED : 0), ID_MAX_FILES_10, L"10個");
    AppendMenuW(maxFilesMenu, MF_STRING | (g_settings.maxBackupFiles == 30 ? MF_CHECKED : 0), ID_MAX_FILES_30, L"30個");
    AppendMenuW(maxFilesMenu, MF_STRING | (g_settings.maxBackupFiles == 50 ? MF_CHECKED : 0), ID_MAX_FILES_50, L"50個");
    AppendMenuW(maxFilesMenu, MF_STRING | (g_settings.maxBackupFiles == 100 ? MF_CHECKED : 0), ID_MAX_FILES_100, L"100個");
    AppendMenuW(maxFilesMenu, MF_STRING | (g_settings.maxBackupFiles == 9999 ? MF_CHECKED : 0), ID_MAX_FILES_UNLIMITED, L"無制限");
    AppendMenuW(newMenu, MF_POPUP, (UINT_PTR)maxFilesMenu, L"最大バックアップ数(&M) >");

    AppendMenuW(newMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(newMenu, MF_STRING, ID_ABOUT, L"このプラグインについて(&H)");

    InsertMenuW(menu, GetMenuItemCount(menu) - 1, MF_POPUP | MF_BYPOSITION, (UINT_PTR)newMenu, L"自動バックアップ(&K)");
    DrawMenuBar(getHWND());
}

// [変更箇所]
void CPlugin::updateMenu() {
    if (!m_hMenu) return;

    // メインメニューのチェック状態を更新
    CheckMenuItem(m_hMenu, ID_TOGGLE_AUTO, g_settings.autoBackupEnabled ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m_hMenu, ID_TOGGLE_DIALOG, g_settings.showSuccessDialog ? MF_CHECKED : MF_UNCHECKED);

    // サブメニューのハンドルを取得
    // createMenuの構成から、バックアップ間隔は6番目(index 5), 最大バックアップ数は7番目(index 6)のメニュー項目
    HMENU intervalMenu = GetSubMenu(m_hMenu, 5);
    HMENU maxFilesMenu = GetSubMenu(m_hMenu, 6);

    // バックアップ間隔のチェック状態を更新
    if (intervalMenu) {
        UINT intervalIds[] = { ID_INTERVAL_1, ID_INTERVAL_3, ID_INTERVAL_5, ID_INTERVAL_10, ID_INTERVAL_15, ID_INTERVAL_30, ID_INTERVAL_60 };
        int intervalValues[] = { 1, 3, 5, 10, 15, 30, 60 };
        for (int i = 0; i < sizeof(intervalIds) / sizeof(UINT); ++i) {
            CheckMenuItem(intervalMenu, intervalIds[i], (g_settings.intervalMinutes == intervalValues[i]) ? MF_CHECKED : MF_UNCHECKED);
        }
    }

    // 最大ファイル数のチェック状態を更新
    if (maxFilesMenu) {
        UINT maxFilesIds[] = { ID_MAX_FILES_10, ID_MAX_FILES_30, ID_MAX_FILES_50, ID_MAX_FILES_100, ID_MAX_FILES_UNLIMITED };
        int maxFilesValues[] = { 10, 30, 50, 100, 9999 };
        for (int i = 0; i < sizeof(maxFilesIds) / sizeof(UINT); ++i) {
            CheckMenuItem(maxFilesMenu, maxFilesIds[i], (g_settings.maxBackupFiles == maxFilesValues[i]) ? MF_CHECKED : MF_UNCHECKED);
        }
    }

    // メニューを再描画
    DrawMenuBar(getHWND());
}
// [/変更箇所]

UINT CPlugin::getBackupMenuId() const { return ID_BACKUP_NOW; }
UINT CPlugin::getAboutMenuId() const { return ID_ABOUT; }

fs::path CPlugin::getCurrentPmmPath() {
    // ウィンドウタイトルからPMMファイルパスを取得
    wchar_t windowTitle[MAX_PATH];
    GetWindowTextW(getHWND(), windowTitle, MAX_PATH);

    std::wstring titleStr(windowTitle);
    size_t startPos = titleStr.find(L'[');
    size_t endPos = titleStr.find(L']');

    if (startPos == std::wstring::npos || endPos == std::wstring::npos || startPos >= endPos) {
        // タイトルから取得できない場合はMMDMainDataから取得
        auto mmdData = mmp::getMMDMainData();
        if (mmdData && mmdData->pmm_path[0] != L'\0') {
            return fs::path(mmdData->pmm_path);
        }
        return fs::path();
    }

    return fs::path(titleStr.substr(startPos + 1, endPos - startPos - 1));
}

void CPlugin::cleanupOldBackups(const fs::path& backupDir) {
    if (!fs::exists(backupDir) || g_settings.maxBackupFiles <= 0 || g_settings.maxBackupFiles >= 9999) return;

    // バックアップファイルのリストを作成
    std::vector<fs::path> backupFiles;
    for (const auto& entry : fs::directory_iterator(backupDir)) {
        if (entry.path().extension() == L".pmm") {
            // 現在のPMMファイル名を含むバックアップのみを対象にする
            fs::path currentPmm = getCurrentPmmPath();
            if (currentPmm.empty()) continue;

            std::wstring backupName = entry.path().stem().wstring();
            std::wstring originalName = currentPmm.stem().wstring();

            // バックアップファイル名が元のファイル名で始まるか確認
            if (backupName.find(originalName) == 0) {
                backupFiles.push_back(entry.path());
            }
        }
    }

    // ファイル数が上限を超えている場合
    if (backupFiles.size() > static_cast<size_t>(g_settings.maxBackupFiles)) {
        // ファイル名（タイムスタンプ）でソート
        std::sort(backupFiles.begin(), backupFiles.end());

        // 古いファイルから削除
        size_t filesToDelete = backupFiles.size() - g_settings.maxBackupFiles;
        for (size_t i = 0; i < filesToDelete; i++) {
            fs::remove(backupFiles[i]);

            // 対応するemmファイルも削除
            fs::path emmPath = backupFiles[i];
            emmPath.replace_extension(L".emm");
            if (fs::exists(emmPath)) {
                fs::remove(emmPath);
            }
        }
    }
}

void CPlugin::triggerSave(bool forceDialog) {
    fs::path currentPmmPath = getCurrentPmmPath();

    if (currentPmmPath.empty() || !fs::exists(currentPmmPath)) {
        MessageBoxW(getHWND(), L"PMMファイルが保存されていないか、見つかりません。\n先に名前を付けて保存してください。", L"エラー", MB_OK | MB_ICONWARNING);
        return;
    }

    // まず現在の状態を保存（Ctrl+S相当）
    SendMessage(getHWND(), WM_COMMAND, 57603, 0);  // ID_FILE_SAVE
    Sleep(300);  // 保存完了を待つ

    // バックアップフォルダの作成（PMMファイルと同じ階層）
    fs::path backupDir = currentPmmPath.parent_path() / L"Backup";

    if (!fs::exists(backupDir)) {
        fs::create_directories(backupDir);
    }

    // タイムスタンプ付きファイル名を生成
    auto now = std::chrono::system_clock::now();
    time_t now_c = std::chrono::system_clock::to_time_t(now);
    tm now_tm;
    localtime_s(&now_tm, &now_c);
    std::wstringstream ss;
    ss << currentPmmPath.stem().wstring()
        << L"_"
        << std::put_time(&now_tm, L"%Y%m%d_%H%M%S")
        << L".pmm";

    fs::path backupPath = backupDir / ss.str();

    // ファイルをコピー
    if (CopyFileW(currentPmmPath.c_str(), backupPath.c_str(), FALSE)) {
        // emmファイルもコピー
        fs::path emmPath = currentPmmPath;
        emmPath.replace_extension(L".emm");
        if (fs::exists(emmPath)) {
            fs::path backupEmmPath = backupPath;
            backupEmmPath.replace_extension(L".emm");
            CopyFileW(emmPath.c_str(), backupEmmPath.c_str(), FALSE);
        }

        // 古いバックアップを削除
        cleanupOldBackups(backupDir);

        // 成功メッセージ（設定または強制表示）
        if (g_settings.showSuccessDialog || forceDialog) {
            std::wstring msg = L"バックアップを作成しました:\n" + backupPath.filename().wstring();
            MessageBoxW(getHWND(), msg.c_str(), L"バックアップ完了", MB_OK | MB_ICONINFORMATION);
        }

        // 最後のバックアップ時刻を更新
        m_lastBackupTime = std::chrono::steady_clock::now();

    }
    else {
        MessageBoxW(getHWND(), L"バックアップに失敗しました。", L"エラー", MB_OK | MB_ICONERROR);
    }
}

void CPlugin::backupWorker() {
    // 初回は少し待つ
    std::this_thread::sleep_for(std::chrono::seconds(10));

    m_lastBackupTime = std::chrono::steady_clock::now();

    while (m_isThreadRunning) {
        // 1秒ごとにチェック
        std::this_thread::sleep_for(std::chrono::seconds(1));

        if (!m_isThreadRunning) break;

        // 自動バックアップが無効なら何もしない
        if (!g_settings.autoBackupEnabled) continue;

        // 経過時間をチェック
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - m_lastBackupTime);

        if (elapsed.count() >= g_settings.intervalMinutes) {
            // MMDウィンドウがアクティブな場合のみバックアップ
            if (GetForegroundWindow() == getHWND()) {
                fs::path currentPath = getCurrentPmmPath();
                if (!currentPath.empty() && fs::exists(currentPath)) {
                    triggerSave(false);  // 自動バックアップは設定に従う
                }
            }
            else {
                // アクティブでない場合も時間をリセット（次回アクティブ時に即バックアップしないように）
                m_lastBackupTime = now;
            }
        }
    }
}

// --- プラグインのエクスポート ---
MMD_PLUGIN_API int version() {
    return 3;
}

MMD_PLUGIN_API MMDPluginDLL3* create3(IDirect3DDevice9*) {
    return g_pPlugin;
}

MMD_PLUGIN_API void destroy3(MMDPluginDLL3*) {}