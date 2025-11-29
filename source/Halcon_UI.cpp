#include "Halcon_Def.h"

int CALLBACK WM_Close_Event(HWINDOW hWindow, UINT nFlags, POINT *pPt, BOOL *pbHandled)
{
    int ret = MessageBoxA(XWnd_GetHWND(hWindow), u8"确认关闭吗？", u8"关闭确认", MB_YESNO);
    if (ret == IDYES)
    {
        HTuple hv_Message;
        CreateMessage(&hv_Message);
        SetMessageTuple(hv_Message, "CMD", 0);
        EnqueueMessage(outQueue, hv_Message, HTuple(), HTuple());
        ClearMessage(hv_Message);
    }
    else
    {
        *pbHandled = TRUE;
    }
    return 0;
}
int CALLBACK WM_CREATE_Event(HWINDOW hWindow, UINT nFlags, POINT *pPt, BOOL *pbHandled)
{
    // 通知初始化完成
    HTuple hv_Message;
    CreateMessage(&hv_Message);
    SetMessageTuple(hv_Message, "CMD", 1);
    EnqueueMessage(outQueue, hv_Message, HTuple(), HTuple());
    ClearMessage(hv_Message);

    return 0;
}

DWORD WINAPI ThreadFunc(LPVOID lpParam)
{
    // 确保线程是GUI线程
    if (!IsGUIThread(TRUE))
    {
        MessageBox(NULL, TEXT("Failed to convert to GUI thread"), TEXT("Error"), MB_OK);
        return -1;
    }

    XInitXCGUI(0);
                                                                                                                                                                                                                   // 初始化
    HWINDOW hWindow = XWnd_Create(0, 0, 1920, 1040, L"YouEyE", NULL, window_style_caption | window_style_border | window_style_center | window_style_icon | window_style_title | window_style_btn_min | window_style_btn_close); // 创建窗口
    XWnd_RegEventC1(hWindow, WM_CLOSE, WM_Close_Event);
    XWnd_RegEventC1(hWindow, WM_CREATE, WM_CREATE_Event);
    std::vector<按钮类> buttons;
    buttons.reserve(64); // 预分配空间

    DEFHalconForPr
    {
        GetDictTuple(inDict, HTuple(hv_GenParamValue[hv_Index]), &hv_ControlHandle);
        GetDictTuple(hv_ControlHandle, u8"控件类型", &hv_ControlType);
        if (0 != (int(hv_ControlType == HTuple(u8"按钮"))))
        {
            GetDictTuple_hv_ID_hv_X_hv_Y_hv_CX_hv_CY_hv_pName_hv_Text
                buttons.emplace_back(
                    hWindow, // 窗口句柄
                    hv_ID,   // C_ID: 
                    hv_X,    // x坐标
                    hv_Y,    // y坐标
                    hv_CX,   // 宽度
                    hv_CY,   // 高度
                    hv_pName // 按钮名称
                );
        }
        else if (0 != (int(hv_ControlType == HTuple(u8"参数"))))
        {
            GetDictTuple_hv_ID_hv_X_hv_Y_hv_CX_hv_CY_hv_pName_hv_Text
        }
        else if (0 != (int(hv_ControlType == HTuple(u8"树型"))))
        {
            GetDictTuple_hv_ID_hv_X_hv_Y_hv_CX_hv_CY_hv_pName_hv_Text
        }
        else if (0 != (int(hv_ControlType == HTuple(u8"文件"))))
        {
            GetDictTuple_hv_ID_hv_X_hv_Y_hv_CX_hv_CY_hv_pName_hv_Text
        }
        else if (0 != (int(hv_ControlType == HTuple(u8"文件夹"))))
        {
            GetDictTuple_hv_ID_hv_X_hv_Y_hv_CX_hv_CY_hv_pName_hv_Text
        }
        else if (0 != (int(hv_ControlType == HTuple(u8"图片"))))
        {
            GetDictTuple_hv_ID_hv_X_hv_Y_hv_CX_hv_CY_hv_pName_hv_Text
        }
        else if (0 != (int(hv_ControlType == HTuple(u8"文本"))))
        {
            GetDictTuple_hv_ID_hv_X_hv_Y_hv_CX_hv_CY_hv_pName_hv_Text
        }
    }

    XWnd_ShowWindow(hWindow, SW_SHOW); // 显示窗口
    // DWORD threadID = 0;                // 创建后台线程,然后在后台线程中操作UI
    // HANDLE hHandle = CreateThread(NULL, 0, MyThreadFunction, hWindow, 0, &threadID);
    // CloseHandle(hHandle);
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

    inDict = HInDictHandle; // 假设 HTuple 支持拷贝构造
    outQueue = HOutQueueHandle;



    // 创建线程
    HANDLE hThread = CreateThread(NULL, 0, ThreadFunc, NULL, 0, NULL);
    if (!hThread)
    {
        MessageBox(NULL, TEXT("Failed to create thread"), TEXT("Error"), MB_OK);
        return H_MSG_TRUE;
    }
    return H_MSG_TRUE;
}

