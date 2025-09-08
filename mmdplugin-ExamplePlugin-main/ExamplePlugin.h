#pragma once
#include "stdafx.h"
#include <thread>
#include <atomic>
#include <chrono>
#include <experimental/filesystem>

namespace fs = std::experimental::filesystem;

// MMDPlugin.h が提供する mmp 名前空間を利用する
using namespace mmp;

class CPlugin : public MMDPluginDLL3 {
public:
    CPlugin(HMODULE hModule);
    ~CPlugin();

    const char* getPluginTitle() const override { return "AutoBackup Plugin"; }
    void start() override;
    void stop() override;

    // パブリックメソッド
    void triggerSave(bool forceDialog);
    void openBackupFolder();
    void showSettings();
    UINT getBackupMenuId() const;
    UINT getAboutMenuId() const;

    void updateMenu(); // updateMenuを追加

private:
    void createMenu();
    fs::path getCurrentPmmPath();
    void cleanupOldBackups(const fs::path& backupDir);

    HMODULE m_hModule;
    HMENU m_hMenu;  // メニューハンドル
    UINT m_nBackupNowMenuId;
    UINT m_nAboutMenuId;

    // バックグラウンド処理用
    std::thread m_thread;
    std::atomic<bool> m_isThreadRunning;
    void backupWorker();

    // 最後のバックアップ時刻
    std::chrono::steady_clock::time_point m_lastBackupTime;
};