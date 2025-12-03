#include "Halcon_Def.h"
struct FrameworkThreadParams
{
    HTuple inDict;
    HTuple outQueue;
    QueueHandle inQueue;

    FrameworkThreadParams(const HTuple &inDict_,
                          const HTuple &outQueue_,
                          QueueHandle inQueue_)
        : inDict(inDict_), outQueue(outQueue_), inQueue(inQueue_)
    {
    }
};
// 在回调函数中操作UI

DWORD WINAPI ThreadFunc(LPVOID lpParam)
{
    FrameworkThreadParams *params = static_cast<FrameworkThreadParams *>(lpParam);
    if (!params)
    {
        return -1;
    }

    // 确保线程是GUI线程
    if (!IsGUIThread(TRUE))
    {
        MessageBox(NULL, TEXT("Failed to convert to GUI thread"), TEXT("Error"), MB_OK);
        return -1;
    }

    XInitXCGUI(0);

    // 初始化
    // HWINDOW hWindow = XWnd_Create(0, 0, 1920, 1040, L"YouEyE", NULL, window_style_caption | window_style_border | window_style_center | window_style_icon | window_style_title | window_style_btn_min | window_style_btn_close); // 创建窗口
     HTuple hv_WVAL,hv_HVAL;
    GetDictTuple(params->inDict, u8"宽", &hv_WVAL);
    GetDictTuple(params->inDict, u8"高", &hv_HVAL);

   
    窗口类 窗口(0, 0, hv_WVAL, hv_HVAL, params->outQueue, params->inQueue);
    RemoveDictKey(params->inDict, u8"宽");
    RemoveDictKey(params->inDict, u8"高");

    HTuple hv_Message;
    CreateMessage(&hv_Message);
    std::vector<按钮类> buttons;
    buttons.reserve(64); // 预分配空间
    std::vector<参数类> Prams;
    Prams.reserve(64); // 预分配空间
    DEFHalconForPr
    {
        GetDictTuple(params->inDict, HTuple(hv_GenParamValue[hv_Index]), &hv_ControlHandle);
        GetDictTuple(hv_ControlHandle, u8"控件类型", &hv_ControlType);
        if (0 != (int(hv_ControlType == HTuple(u8"按钮"))))
        {
            HGetDictTuples
                buttons.emplace_back(
                    窗口.m_hWindow, // 窗口句柄
                    hv_ID,          // C_ID:
                    hv_X,           // x坐标
                    hv_Y,           // y坐标
                    hv_CX,          // 宽度
                    hv_CY,          // 高度
                    hv_pName,       // 按钮名称
                    params->outQueue);
        }
        else if (0 != (int(hv_ControlType == HTuple(u8"参数"))))
        {
            HGetDictTuples
                HTuple hv_VAL,
                hv_TYPE, hv_LIMIT;
            GetDictTuple(hv_ControlHandle, u8"默认值", &hv_VAL);
            GetDictTuple(hv_ControlHandle, u8"类型", &hv_TYPE);
            GetDictTuple(hv_ControlHandle, u8"限制", &hv_LIMIT);

            Prams.emplace_back(
                窗口.m_hWindow,   // 窗口句柄
                hv_ID,            // C_ID:
                hv_X,             // x坐标
                hv_Y,             // y坐标
                hv_CX,            // 宽度
                hv_CY,            // 高度
                hv_pName,         // 按钮名称
                params->outQueue, // 消息队列
                hv_VAL,           // 默认值
                hv_TYPE,          // 数据类型
                hv_LIMIT);
        }
        else if (0 != (int(hv_ControlType == HTuple(u8"树型"))))
        {
            HGetDictTuples
        }
        else if (0 != (int(hv_ControlType == HTuple(u8"文件"))))
        {
            HGetDictTuples
        }
        else if (0 != (int(hv_ControlType == HTuple(u8"文件夹"))))
        {
            HGetDictTuples
        }
        else if (0 != (int(hv_ControlType == HTuple(u8"图片"))))
        {
            HGetDictTuples
                HTuple hv_mode,
                HwindowsHandle;
            GetDictTuple(hv_ControlHandle, u8"模式", &hv_mode);

            OpenWindow(hv_Y,  // x坐标
                       hv_X,  // y坐标
                       hv_CX, // 宽度
                       hv_CY, // 高度
                       (_int64)XWnd_GetHWND(窗口.m_hWindow),
                       hv_mode,
                       "",
                       &HwindowsHandle);
          
            SetMessageTuple(hv_Message, hv_pName + "_" + hv_ID, HwindowsHandle);
            
        }
        else if (0 != (int(hv_ControlType == HTuple(u8"文本"))))
        {
            HGetDictTuples
                HTuple hv_mode,
                hv_BorderWidth, hv_BorderColour, hv_BackgroundColour, HTextwindowsHandle;

            GetDictTuple(hv_ControlHandle, u8"模式", &hv_mode);
            GetDictTuple(hv_ControlHandle, u8"线框", &hv_BorderWidth);
            GetDictTuple(hv_ControlHandle, u8"线框色", &hv_BorderColour);
            GetDictTuple(hv_ControlHandle, u8"背景色", &hv_BackgroundColour);
            OpenTextwindow(hv_Y,  // x坐标
                           hv_X,  // y坐标
                           hv_CX, // 宽度
                           hv_CY, // 高度
                           hv_BorderWidth,
                           hv_BorderColour,
                           hv_BackgroundColour,
                           (_int64)XWnd_GetHWND(窗口.m_hWindow),
                           hv_mode,
                           "",
                           &HTextwindowsHandle);
            //ClearWindow(HTextwindowsHandle);


            SetMessageTuple(hv_Message, hv_pName + "_" + hv_ID, HTextwindowsHandle);
        }
    }

    XWnd_ShowWindow(窗口.m_hWindow, SW_SHOW);
    // 通知初始化完成
    SetMessageTuple(hv_Message, "CMD", 1);
    EnqueueMessage(params->outQueue, hv_Message, HTuple(), HTuple());
    ClearMessage(hv_Message);

    XRunXCGUI();  // 运行
    XExitXCGUI(); // 释放资源
    return -1;
}

Herror Open_Frameworkwindows(Hproc_handle proc_handle)
{
    Hcpar *InDictHandle; // 名称
    Hcpar *OutQueueHandle;

    INT4_8 num;
    HGetPPar(proc_handle, 1, &InDictHandle, &num);
    HTuple HInDictHandle(InDictHandle, num);
    HGetPPar(proc_handle, 2, &OutQueueHandle, &num);
    HTuple HOutQueueHandle(OutQueueHandle, num);

    QueueHandle inQueue;
    create_String_queue(&inQueue);

    // 动态分配参数包
    FrameworkThreadParams *params = new FrameworkThreadParams(HInDictHandle, HOutQueueHandle, inQueue);

    // 创建线程
    HANDLE hThread = CreateThread(NULL, 0, ThreadFunc, params, 0, NULL);
    if (!hThread)
    {
        MessageBox(NULL, TEXT("Failed to create thread"), TEXT("Error"), MB_OK);
        return H_MSG_TRUE;
    }
    return H_MSG_TRUE;
}
