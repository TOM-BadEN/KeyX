#include <switch.h>
#include <string.h>
#include <stdio.h>
#include "app.hpp"
#include "language.hpp"

// libnx fake heap initialization
extern "C" {

// // 内部堆的大小（根据需要调整） 12KB
// #define INNER_HEAP_SIZE 0x3000

// 内部堆的大小（根据需要调整）62KB
// #define INNER_HEAP_SIZE 0xF800

// 内部堆的大小（根据需要调整）256KB
#define INNER_HEAP_SIZE 0x40000

// 系统模块不应使用applet相关功能
u32 __nx_applet_type = AppletType_None;

// 系统模块通常只需要使用一个文件系统会话
u32 __nx_fs_num_sessions = 1;

// Newlib堆配置函数（使malloc/free能够工作）
void __libnx_initheap(void) {
    static u8 inner_heap[INNER_HEAP_SIZE];
    extern void* fake_heap_start;
    extern void* fake_heap_end;

    // 配置newlib堆
    fake_heap_start = inner_heap;
    fake_heap_end   = inner_heap + sizeof(inner_heap);
}

void __appInit(void) {
  smInitialize();
  setsysInitialize();
  SetSysFirmwareVersion fw;
  if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw)))
      hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
  setsysExit();
  setInitialize();
  getSetNotifLanguage();
  setExit();
  fsInitialize();
  fsdevMountSdmc();
  hidInitialize();
  hiddbgInitialize();
  hidsysInitialize();
  pmdmntInitialize();
  pmshellInitialize();

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
}

void __appExit(void) {
  pdmqryExit();
  pmshellExit();
  pmdmntExit();     
  hidsysExit();     
  hiddbgExit();     
  hidExit();        
  fsdevUnmountAll(); 
  fsExit();         
  smExit();         
}

}

int main() {
  App app;
  app.Loop();
  return 0;
}
