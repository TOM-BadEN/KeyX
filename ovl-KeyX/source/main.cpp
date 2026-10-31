#define TESLA_INIT_IMPL
#include <tesla.hpp>
#include "main_menu.hpp"
#include "language.hpp"
#include "refresh.hpp"
#include "game.hpp"
#include "Tthread.hpp"
#include "updater_data.hpp"



namespace {

    constexpr SocketInitConfig socketConfig = {
        .tcp_tx_buf_size     = 8 * 1024,
        .tcp_rx_buf_size     = 16 * 1024,
        .tcp_tx_buf_max_size = 32 * 1024,
        .tcp_rx_buf_max_size = 64 * 1024,
        .udp_tx_buf_size     = 512,
        .udp_rx_buf_size     = 512,
        .sb_efficiency       = 1,
        .bsd_service_type    = BsdServiceType_Auto
    };
    
    // 设置更新检查标志
    void checkUpdate() {
        UpdaterData data;
        UpdateInfo info = data.getUpdateInfo();
        if (info.success) UpdateChecker::g_hasNewVersion = data.hasNewVersion(info.version, APP_VERSION_STRING);
    }

}
// KeyX 特斯拉覆盖层主类
class KeyXOverlay : public tsl::Overlay {

private:

    bool m_isFirstShow = true;          // 是否是第一次显示

public:
    // 初始化系统服务
    virtual void initServices() override 
    {            
                                 
        pmdmntInitialize();                                           // 进程管理服务
        setInitialize();                                              // 初始化set服务（获取系统语言）
        nsInitialize();                                               // 初始化ns服务
        LanguageManager::initialize();                                // 初始化语言系统
        setExit();                                                    // 退出set服务

        // 直接持有 pdm:qry 原始连接，供现有焦点检测使用，退出时由 pdmqryExit() 释放。
        // 原克隆方案用于缓解服务入口的会话名额限制；在固件 23.0.1 与 SaltyNX、Horizon-OC 共存测试中，
        // 克隆并替换连接的启动流程会崩溃，改用原始连接后功能及焦点检测正常，因此暂时禁用克隆。
        // 尚未查明克隆崩溃的具体原因及直接连接的资源限制；以后调整需重新验证插件共存和焦点检测。
        pdmqryInitialize();

        // 保留完整旧流程供后续排查；若恢复，需同时移除上方直接初始化，避免重复连接初始化。
        /*
        Result rc = pdmqryInitialize();
        if (R_SUCCEEDED(rc)) {
            Service* pdmqrySrv = pdmqryGetServiceSession();
            Service pdmqryClone;
            serviceClone(pdmqrySrv, &pdmqryClone);
            serviceClose(pdmqrySrv);
            memcpy(pdmqrySrv, &pdmqryClone, sizeof(Service));
        }
        */
        GameMonitor::loadWhitelist();                                 // 加载白名单
        socketInitialize(&socketConfig);
        curl_global_init(CURL_GLOBAL_DEFAULT);
        Thd::start(checkUpdate);                                      // 启动更新检查线程
    }
    
    // 退出系统服务
    virtual void exitServices() override 
    {
        Thd::stop();                // 清理线程
        curl_global_cleanup();
        socketExit();
        nsExit();                   // 退出 ns 服务
        pdmqryExit();               // 退出 pdmqry 服务
        pmdmntExit();               // 退出进程管理服务
    }

    // 覆盖层显示时调用，用于更新游戏信息
    virtual void onShow() override {
        if (m_isFirstShow) m_isFirstShow = false;
        else Refresh::RefrRequest(Refresh::OnShow);
    }

    // 加载初始GUI界面
    virtual std::unique_ptr<tsl::Gui> loadInitialGui() override {
        return initially<MainMenu>();  // 返回主菜单界面
    }
};

// 程序入口点
int main(int argc, char **argv) {
    rename("sdmc:/config/KeyX/Macros", "sdmc:/config/KeyX/macros");
    return tsl::loop<KeyXOverlay>(argc, argv);
}