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

// 内部堆的大小（根据需要调整）768KB
#define INNER_HEAP_SIZE 0xC0000

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

  // pdmqry服务会话数量限制，用这个方法可以绕开限制，以防和别的插件或者游戏发生冲突引发崩溃
  Result rc = pdmqryInitialize();
  if (R_SUCCEEDED(rc)) {
      Service* pdmqrySrv = pdmqryGetServiceSession();
      Service pdmqryClone;
      serviceClone(pdmqrySrv, &pdmqryClone);
      serviceClose(pdmqrySrv);
      memcpy(pdmqrySrv, &pdmqryClone, sizeof(Service));
  }
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
