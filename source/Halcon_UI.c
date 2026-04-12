#include <stdio.h>
#include "Halcon_UI.h"

// 创建 WebSocket 服务器的 C 包装器
Herror CCreateWebServer(Hproc_handle proc_handle)
{
    return HCreateWebServer(proc_handle);
}

// 接收数据的 C 包装器
Herror CRecvWebData(Hproc_handle proc_handle)
{
    return HRecvWebData(proc_handle);
}

// 发送数据的 C 包装器
Herror CSendWebData(Hproc_handle proc_handle)
{
    return HSendWebData(proc_handle);
}

// 关闭服务器的 C 包装器
Herror CCloseWebServer(Hproc_handle proc_handle)
{
    return HCloseWebServer(proc_handle);
}
