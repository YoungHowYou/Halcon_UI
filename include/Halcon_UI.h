#include "Halcon.h"

#ifdef _WIN32
  #define EXPORTS_API __declspec(dllexport)
#else
  #define EXPORTS_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#pragma region WebServer

// 创建 WebSocket 服务器
EXPORTS_API Herror HCreateWebServer(Hproc_handle proc_handle);

// 接收数据
EXPORTS_API Herror HRecvWebData(Hproc_handle proc_handle);

// 发送数据
EXPORTS_API Herror HSendWebData(Hproc_handle proc_handle);

// 关闭 WebSocket 服务器
EXPORTS_API Herror HCloseWebServer(Hproc_handle proc_handle);

#pragma endregion

#ifdef __cplusplus
}
#endif
