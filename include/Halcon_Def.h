#include "Halcon_UI.h"
#include "HalconCpp.h"
#include "xcgui.h"
#include <windows.h>
#include <vector>
#include "StringQueue.h"
using namespace HalconCpp;

#define DEFHalconForPr                                                 \
    HTuple hv_GenParamValue, hv_Index;                                 \
    HTuple hv_ControlHandle, hv_ControlType, hv_ID, hv_X, hv_Y;        \
    HTuple hv_CX, hv_CY, hv_pName, hv_Text;                            \
    GetDictParam(params->inDict, "keys", HTuple(), &hv_GenParamValue); \
    HTuple end_val8 = hv_GenParamValue.TupleLength() - 1;              \
    HTuple step_val8 = 1;                                              \
    for (hv_Index = 0; hv_Index.Continue(end_val8, step_val8); hv_Index += step_val8)

#define HGetDictTuples                                  \
    GetDictTuple(hv_ControlHandle, u8"控件ID", &hv_ID); \
    GetDictTuple(hv_ControlHandle, "X", &hv_X);         \
    GetDictTuple(hv_ControlHandle, "Y", &hv_Y);         \
    GetDictTuple(hv_ControlHandle, "CX", &hv_CX);       \
    GetDictTuple(hv_ControlHandle, "CY", &hv_CY);       \
    GetDictTuple(hv_ControlHandle, u8"标题", &hv_pName);

// 基类声明

class 参数修改对话框类
{
public:
    HWINDOW 父窗口;
    HWINDOW 设置窗口;
    HTuple 参数字典;
    const wchar_t *标题栏;
    HTuple 键值数组;
    HELE 参数值容器;
    HELE 参数值提示容器;
    HTuple 参数值容器字典;
    HTuple 参数值;
    HTuple 权限;
    HTuple 参数类型;
    HTuple 参数限制;
    HTuple outQueue;

    参数修改对话框类(HWINDOW m_hWindows, HTuple hv_ParmDict, HTuple hv_Key_Tuple, const wchar_t *TiteString)
    {
        父窗口 = m_hWindows;
        参数字典 = hv_ParmDict;
        标题栏 = TiteString;
        键值数组 = hv_Key_Tuple;
        Init();
    }
    ~参数修改对话框类()
    {
    }

private:
    void 获取属性和值(HTuple hv_ParmDict, HTuple *hv_Value, HTuple *hv_Types, HTuple *hv_Limits, HTuple *hv_Competence, HTuple *hv_Text)
    {

        // Local iconic variables

        // Local control variables
        HTuple hv_TextA, hv_TextB, hv_LimitDict, hv_GenParamValue;
        HTuple hv_Index, hv_Limitc, hv_TextC;

        GetDictTuple(hv_ParmDict, "Value", &(*hv_Value));
        GetDictTuple(hv_ParmDict, "Type", &(*hv_Types));
        GetDictTuple(hv_ParmDict, "Competence", &(*hv_Competence));
        hv_TextA = //'数据类型：'
            "\346\225\260\346\215\256\347\261\273\345\236\213\357\274\232" + (*hv_Types);
        hv_TextB = //'参数范围：['
            "\345\217\202\346\225\260\350\214\203\345\233\264\357\274\232[";
        (*hv_Limits) = HTuple();
        GetDictTuple(hv_ParmDict, "Limit", &hv_LimitDict);
        try
        {

            GetDictParam(hv_LimitDict, "keys", HTuple(), &hv_GenParamValue);
            {
                HTuple end_val9 = (hv_GenParamValue.TupleLength()) - 1;
                HTuple step_val9 = 1;
                for (hv_Index = 0; hv_Index.Continue(end_val9, step_val9); hv_Index += step_val9)
                {
                    GetDictTuple(hv_LimitDict, HTuple(hv_GenParamValue[hv_Index]), &hv_Limitc);
                    (*hv_Limits) = (*hv_Limits).TupleConcat(hv_Limitc);
                    if (0 != (int(hv_Index == ((hv_GenParamValue.TupleLength()) - 1))))
                    {
                        hv_TextB += hv_Limitc;
                    }
                    else
                    {
                        hv_TextB = (hv_TextB + hv_Limitc) + HTuple(",");
                    }
                }
            }
            hv_TextB += HTuple("]");
        }
        catch (HException &HDevExpDefaultException)
        {
            hv_TextB = u8"参数范围：[]";
            (*hv_Limits) = HTuple();
        }

        hv_TextC = //'权限等级：'
            "\346\235\203\351\231\220\347\255\211\347\272\247\357\274\232" + (*hv_Competence);
        (*hv_Text) = (((hv_TextA + "\n") + hv_TextB) + "\n") + hv_TextC;
        return;
    }
    int 整数类型文本框值改变(BOOL *pbHandled)
    {
        int ret;
        wchar_t pOut[6];
        int len = XEdit_GetText(参数值容器, pOut, 6);
        // ret = MessageBox(NULL, pOut, u8"参数显示", MB_YESNO);
        HTuple hv_TupleV;
        HTuple hv_TupleVChar(pOut);

        TupleNumber(hv_TupleVChar, &hv_TupleV);
        if (hv_TupleV.L() < 参数限制[0].L() || hv_TupleV.L() > 参数限制[1].L())
        {

            ret = MessageBox(NULL, u8"参数不合规，请看上方提示", u8"参数显示", MB_YESNO);
            return 0;
        }
        SetDictTuple(参数值容器字典, "Value", hv_TupleV);
        ret = MessageBox(NULL, u8"完成参数修改", u8"完成参数修改", MB_YESNO);
        return 0;
    }
    int 浮点类型文本框值改变(BOOL *pbHandled)
    {
        int ret;
        wchar_t pOut[16];
        int len = XEdit_GetText(参数值容器, pOut, 16);
        HTuple hv_TupleV;
        HTuple hv_TupleVChar(pOut);
        // ret = MessageBox(NULL, pOut, u8"参数显示", MB_YESNO);
        TupleNumber(hv_TupleVChar, &hv_TupleV);

        if (hv_TupleV.D() < 参数限制[0].D() || hv_TupleV.D() > 参数限制[1].D())
        {
            ret = MessageBox(NULL, u8"参数不合规，请看上方提示", u8"参数显示", MB_YESNO);
            return 0;
        }
        SetDictTuple(参数值容器字典, "Value", hv_TupleV);
        ret = MessageBox(NULL, u8"完成参数修改", u8"完成参数修改", MB_YESNO);
        return 0;
    }
    int 字符串枚举选择框值改变(BOOL *pbHandled)
    {
        int ret;
        wchar_t pOut[16];
        int len = XEdit_GetText(参数值容器, pOut, 16);
        // ret = MessageBox(NULL, pOut, u8"完成参数修改", MB_YESNO);
        HTuple hv_TupleV(pOut);
        SetDictTuple(参数值容器字典, "Value", hv_TupleV);
        ret = MessageBox(NULL, u8"完成参数修改", u8"完成参数修改", MB_YESNO);
        return 0;
    }
    int 整数枚举选择框值改变(BOOL *pbHandled)
    {
        int ret;
        wchar_t pOut[6];
        int len = XEdit_GetText(参数值容器, pOut, 6);
        HTuple hv_TupleV;
        HTuple hv_TupleVChar(pOut);
        TupleNumber(hv_TupleVChar, &hv_TupleV);
        SetDictTuple(参数值容器字典, "Value", hv_TupleV);
        ret = MessageBox(NULL, u8"完成参数修改", u8"完成参数修改", MB_YESNO);
        return 0;
    }
    int 浮点数枚举选择框值改变(BOOL *pbHandled)
    {
        int ret;
        wchar_t pOut[16];
        int len = XEdit_GetText(参数值容器, pOut, 16);
        HTuple hv_TupleV;
        HTuple hv_TupleVChar(pOut);
        TupleNumber(hv_TupleVChar, &hv_TupleV);
        SetDictTuple(参数值容器字典, "Value", hv_TupleV);
        ret = MessageBox(NULL, u8"完成参数修改", u8"完成参数修改", MB_YESNO);
        return 0;
    }
    int 字符串类型文本框改变(BOOL *pbHandled)
    {
        int ret;
        wchar_t pOut[256];
        int len = XEdit_GetText(参数值容器, pOut, 256);
        HTuple hv_TupleV(pOut);
        SetDictTuple(参数值容器字典, "Value", hv_TupleV);
        ret = MessageBox(NULL, u8"完成参数修改", u8"完成参数修改", MB_YESNO);
        return 0;
    }
    int 文件路径改变(BOOL *pbHandled)
    {
        int ret;
        wchar_t pOut[256];
        int len = XEdit_GetText(参数值容器, pOut, 256);
        HTuple hv_TupleV(pOut);
        SetDictTuple(参数值容器字典, "Value", hv_TupleV);
        ret = MessageBox(NULL, u8"完成参数修改", u8"完成参数修改", MB_YESNO);
        return 0;
    }
    int 文件夹路径改变(BOOL *pbHandled)
    {
        int ret;
        wchar_t pOut[256];
        int len = XEdit_GetText(参数值容器, pOut, 256);
        HTuple hv_TupleV(pOut);
        SetDictTuple(参数值容器字典, "Value", hv_TupleV);
        ret = MessageBox(NULL, u8"完成参数修改", u8"完成参数修改", MB_YESNO);
        return 0;
    }
    void Init()
    {
        //

        HWINDOW 参数修改窗口 = XModalWnd_Create(400, 250, 标题栏, XWnd_GetHWND(父窗口), window_style_btn_close | window_style_title | window_style_icon | window_style_center | window_style_caption | window_style_border | WS_EX_TOPMOST);
        HTuple hv_ParmDict;
        hv_ParmDict = 参数字典;
        for (int i = (键值数组.TupleLength() - 2); i > -1; i--)
        {
            // ret = MessageBox(NULL, 键值数组[i].S().TextW(), u8"参数显示", MB_YESNO);
            HTuple hv_TParmDict;
            GetDictTuple(hv_ParmDict, 键值数组[i], &hv_TParmDict);
            hv_ParmDict = hv_TParmDict;
        }
        参数值容器字典 = hv_ParmDict;
        HTuple 参数的属性展示;
        获取属性和值(参数值容器字典, &参数值, &参数类型, &参数限制, &权限, &参数的属性展示);

        参数值提示容器 = XEdit_Create(50, 40, 300, 90, 参数修改窗口);
        XEdit_EnableMultiLine(参数值提示容器, true);
        HTuple 参数值T;
        XEdit_SetText(参数值提示容器, 参数的属性展示.S().TextW());
        HELE 参数值修改按钮 = XBtn_Create(150, 200, 100, 30, L"修改", 参数修改窗口);
        // 参数分类
        {
            if (参数类型.S() == "int")
            {
                参数值容器 = XEdit_Create(50, 150, 300, 30, 参数修改窗口);
                TupleString(参数值, ".0f", &参数值T);
                XEdit_SetText(参数值容器, 参数值T.S().TextW());
                XEle_RegEventCPP(参数值修改按钮, XE_BNCLICK, &参数修改对话框类::整数类型文本框值改变);
            }
            else if (参数类型.S() == "float")
            {
                参数值容器 = XEdit_Create(50, 150, 300, 30, 参数修改窗口);
                TupleString(参数值, ".3f", &参数值T);
                XEdit_SetText(参数值容器, 参数值T.S().TextW());
                XEle_RegEventCPP(参数值修改按钮, XE_BNCLICK, &参数修改对话框类::浮点类型文本框值改变);
            }
            else if (参数类型.S() == "booL")
            {
                HELE hCheck1 = XBtn_Create(50, 150, 100, 20, L"开启", 参数修改窗口);
                HELE hCheck2 = XBtn_Create(150, 150, 100, 20, L"关闭", 参数修改窗口);
                XBtn_SetGroupID(hCheck1, 1);
                XBtn_SetGroupID(hCheck2, 1);

                XBtn_SetTypeEx(hCheck1, button_type_radio);
                XBtn_SetTypeEx(hCheck2, button_type_radio);
                if (0 != (int(参数值 == HTuple("TRUE"))))
                {
                    XBtn_SetCheck(hCheck1, TRUE);
                }
                else
                {
                    XBtn_SetCheck(hCheck2, TRUE);
                }
            }
            else if (参数类型.S() == "int_enum")
            {
                参数值容器 = XComboBox_Create(50, 150, 300, 30, 参数修改窗口);
                XComboBox_SetItemTemplateXML(参数值容器, L"xml-template\\ComboBox_ListBox_Item.xml");
                TupleString(参数值, ".0f", &参数值T);
                XEdit_SetText(参数值容器, 参数值T.S().TextW());

                HXCGUI hAdapter = XAdTable_Create();
                XComboBox_BindAdapter(参数值容器, hAdapter);
                XAdTable_AddColumn(hAdapter, XC_NAME1);
                for (int i = 0; i < (参数限制.TupleLength()).I(); i++)
                {
                    TupleString(参数限制[i], ".0f", &参数值T);
                    XAdTable_AddItemText(hAdapter, 参数值T.S().TextW());
                }
                XEle_RegEventCPP(参数值修改按钮, XE_BNCLICK, &参数修改对话框类::整数枚举选择框值改变);
            }
            else if (参数类型.S() == "float_enum")
            {

                参数值容器 = XComboBox_Create(50, 150, 300, 30, 参数修改窗口);
                XComboBox_SetItemTemplateXML(参数值容器, L"xml-template\\ComboBox_ListBox_Item.xml");
                TupleString(参数值, ".3f", &参数值T);
                XEdit_SetText(参数值容器, 参数值T.S().TextW());

                HXCGUI hAdapter = XAdTable_Create();
                XComboBox_BindAdapter(参数值容器, hAdapter);
                XAdTable_AddColumn(hAdapter, XC_NAME1);
                for (int i = 0; i < (参数限制.TupleLength()).I(); i++)
                {
                    TupleString(参数限制[i], ".3f", &参数值T);
                    XAdTable_AddItemText(hAdapter, 参数值T.S().TextW());
                }
                XEle_RegEventCPP(参数值修改按钮, XE_BNCLICK, &参数修改对话框类::浮点数枚举选择框值改变);
            }
            else if (参数类型.S() == "string_enum")
            {
                参数值容器 = XComboBox_Create(50, 150, 300, 30, 参数修改窗口);
                XComboBox_SetItemTemplateXML(参数值容器, L"xml-template\\ComboBox_ListBox_Item.xml");
                XEdit_SetText(参数值容器, 参数值.S().TextW());
                HXCGUI hAdapter = XAdTable_Create();
                XComboBox_BindAdapter(参数值容器, hAdapter);
                XAdTable_AddColumn(hAdapter, XC_NAME1);
                for (int i = 0; i < (参数限制.TupleLength()).I(); i++)
                {
                    XAdTable_AddItemText(hAdapter, 参数限制[i].S().TextW());
                }

                XEle_RegEventCPP(参数值修改按钮, XE_BNCLICK, &参数修改对话框类::字符串枚举选择框值改变);
            }
            else if (参数类型.S() == "path")
            {
                参数值容器 = XEdit_Create(50, 150, 300, 30, 参数修改窗口);
                XEdit_SetText(参数值容器, 参数值.S().TextW());
                XEle_RegEventCPP(参数值修改按钮, XE_BNCLICK, &参数修改对话框类::文件路径改变);
            }
            else if (参数类型.S() == "directory")
            {
                参数值容器 = XEdit_Create(50, 150, 300, 30, 参数修改窗口);
                XEdit_SetText(参数值容器, 参数值.S().TextW());
                XEle_RegEventCPP(参数值修改按钮, XE_BNCLICK, &参数修改对话框类::文件夹路径改变);
            }
            else // 参数类型.S() == "string"
            {
                参数值容器 = XEdit_Create(50, 150, 300, 30, 参数修改窗口);
                XEdit_SetText(参数值容器, 参数值.S().TextW());
                XEle_RegEventCPP(参数值修改按钮, XE_BNCLICK, &参数修改对话框类::字符串类型文本框改变);
            }
        }
        int nResult = XModalWnd_DoModal(参数修改窗口);
    }
};

class 参数设置对话框类
{
public:
    HWINDOW 父窗口;
    HELE 参数树控件;
    HWINDOW 设置窗口;
    HTuple 参数字典;
    HXCGUI 树控件设计器;
    const wchar_t *标题栏;
    const wchar_t *标题栏图标;
    HTuple 数据库序号;
    HFONTX 字体资源;
    HTuple outQueue;
    参数设置对话框类(HWINDOW m_hWindows, HTuple hv_ParmDict, HTuple hv_ID, const wchar_t *TiteString, const wchar_t *IocPath)
    {
        父窗口 = m_hWindows;
        参数字典 = hv_ParmDict;
        标题栏 = TiteString;
        标题栏图标 = IocPath;
        数据库序号 = hv_ID;
        Init();
    }
    ~参数设置对话框类()
    {
        XFont_Destroy(字体资源);
    }

private:
    void HalconAdTree_InsertItemText(HTuple hv_In_id, HTuple hv_keysNmane, HTuple *hv_Out_id)
    {

        int id = XAdTree_InsertItemText(树控件设计器, hv_keysNmane.S().TextW(), hv_In_id, XC_ID_LAST);
        (*hv_Out_id) = id;
        return;
    }
    void Json转树控件(HTuple hv_ParmDict, HTuple hv_In_ID, HTuple *hv_Out_ID)
    {

        // Local control variables
        HTuple hv_keysNmane, hv_Indices, hv_Index, hv_Out_id;
        HTuple hv_ParmDictTuple;

        GetDictParam(hv_ParmDict, "keys", HTuple(), &hv_keysNmane);
        TupleFind(hv_keysNmane, "Value", &hv_Indices);
        if (0 != (int(hv_Indices == -1)))
        {
            {
                HTuple end_val3 = (hv_keysNmane.TupleLength()) - 1;
                HTuple step_val3 = 1;
                for (hv_Index = 0; hv_Index.Continue(end_val3, step_val3); hv_Index += step_val3)
                {
                    HalconAdTree_InsertItemText(hv_In_ID, HTuple(hv_keysNmane[hv_Index]), &hv_Out_id);
                    GetDictTuple(hv_ParmDict, HTuple(hv_keysNmane[hv_Index]), &hv_ParmDictTuple);
                    Json转树控件(hv_ParmDictTuple, hv_Out_id, &(*hv_Out_ID));
                }
            }
        }
        else
        {
            return;
        }
        return;
    }
    void Init()
    {

        字体资源 = XFont_CreateFromFile(L"Fontx\\msyh.ttc", 16);
        XFont_EnableAutoDestroy(字体资源, FALSE);
        设置窗口 = XModalWnd_Create(540, 700, 标题栏, XWnd_GetHWND(父窗口));
        // 设置窗口 = XWnd_Create(0, 0, 600, 600, 标题栏, XWnd_GetHWND(父窗口), window_style_btn_close | window_style_title | window_style_icon | window_style_center | window_style_caption | window_style_border);

        XWnd_SetIcon(设置窗口, XImage_LoadFile(标题栏图标));

        参数树控件 = XTree_Create(20, 40, 500, 600, 设置窗口);
        XTree_SetRowSpace(参数树控件, 5);

        XTree_SetItemTemplateXML(参数树控件, L"xml-template\\Tree_Item.xml");
        树控件设计器 = XAdTree_Create();

        XTree_BindAdapter(参数树控件, 树控件设计器);
        XAdTree_AddColumn(树控件设计器, XC_NAME1);
        HTuple hv_Out_ID;
        Json转树控件(参数字典, 0, &hv_Out_ID);
        HELE 导出参数按钮 = XBtn_Create(100 + (100 + 30) * 0, 700 - 50, 100, 30, L"导出参数", 设置窗口);
        HELE 导入参数按钮 = XBtn_Create(100 + (100 + 30) * 1, 700 - 50, 100, 30, L"导入参数", 设置窗口);
        HELE 保存参数按钮 = XBtn_Create(100 + (100 + 30) * 2, 700 - 50, 100, 30, L"保存参数", 设置窗口);

        XEle_RegEventCPP(参数树控件, XE_TREE_SELECT, &参数设置对话框类::树控件项目选中事件);
        XEle_RegEventCPP(参数树控件, XE_TREE_EXPAND, &参数设置对话框类::树控件项目展开事件);
        XEle_RegEventCPP(参数树控件, XE_TREE_TEMP_CREATE_END, &参数设置对话框类::树控件模板创建完成事件);

        XEle_RegEventCPP(保存参数按钮, XE_BNCLICK, &参数设置对话框类::保存参数按钮按下事件);
        XEle_RegEventCPP(导出参数按钮, XE_BNCLICK, &参数设置对话框类::导出参数按钮按下事件);
        XEle_RegEventCPP(导入参数按钮, XE_BNCLICK, &参数设置对话框类::导入参数按钮按下事件);

        int nResult = XModalWnd_DoModal(设置窗口);
        // XWnd_Show(设置窗口, TRUE); //显示
    }

    int 保存参数按钮按下事件(BOOL *pbHandled)
    {
        HTuple json;
        char *errmsg;
        DictToJson(参数字典, HTuple(), HTuple(), &json);
        int rev;

        HTuple hv_message;
        CreateMessage(&hv_message);
        SetMessageTuple(hv_message, "Cmd", 数据库序号.TupleNumber() + 5);
        SetMessageTuple(hv_message, "Parm", 参数字典);
        EnqueueMessage(outQueue, hv_message, HTuple(), HTuple());
        ClearMessage(hv_message);
        return 0;
    }
    int 导出参数按钮按下事件(BOOL *pbHandled)
    {

        return 0;
    }
    int 导入参数按钮按下事件(BOOL *pbHandled)
    {

        HTuple json;
        char *errmsg;
        DictToJson(参数字典, HTuple(), HTuple(), &json);

        HTuple hv_message;
        CreateMessage(&hv_message);

        SetMessageTuple(hv_message, "Cmd", 数据库序号.TupleNumber() + 5);
        SetMessageTuple(hv_message, "Parm", 参数字典);

        EnqueueMessage(outQueue, hv_message, HTuple(), HTuple());
        ClearMessage(hv_message);

        return 0;
    }
    int 树控件模板创建完成事件(tree_item_ *pItem, int nFlag, BOOL *pbHandled)
    {
        HXCGUI hShapeText = XTree_GetTemplateObject(参数树控件, pItem->nID, 2);
        if (XC_IsHXCGUI(hShapeText, XC_SHAPE_TEXT))
        {
            XShapeText_SetFont(hShapeText, 字体资源);
        }
        //*pbHandled = TRUE;
        return 0;
    }
    void Get_TreeItemKey(int nItemID, HTuple *hv_Tree_Key)
    {
        int ID = XTree_GetParentItem(参数树控件, nItemID);
        HTuple hv_Temp_Tree_Key;

        if (ID != -1)
        {
            hv_Temp_Tree_Key = XTree_GetItemText(参数树控件, ID, 0);
            (*hv_Tree_Key) = (*hv_Tree_Key).TupleConcat(hv_Temp_Tree_Key);
            Get_TreeItemKey(ID, &(*hv_Tree_Key));
            return;
        }
        else
        {

            return;
        }
    }
    int 树控件项目选中事件(int nItemID, BOOL *pbHandled)
    {

        if (XTree_GetFirstChildItem(参数树控件, nItemID) != XC_ID_ERROR)
        {
            *pbHandled = TRUE;
        }
        else
        {
            if (XTree_GetParentItem(参数树控件, nItemID) != XC_ID_ERROR)
            {
                HTuple hv_Tree_Key;
                hv_Tree_Key = HTuple();
                hv_Tree_Key = hv_Tree_Key.TupleConcat(XTree_GetItemText(参数树控件, nItemID, 0));

                Get_TreeItemKey(nItemID, &hv_Tree_Key);
                HTuple 参数修改窗口标题 = u8"参数修改";
                for (int i = (hv_Tree_Key.TupleLength() - 2); i > -1; i--)
                {
                    参数修改窗口标题 = 参数修改窗口标题 + ">" + hv_Tree_Key[i];
                }

                参数修改对话框类 参数修改对话框(设置窗口, 参数字典, hv_Tree_Key, 参数修改窗口标题.S().TextW());
            }
            else
            {
                *pbHandled = TRUE;
            }
        }
        return 0;
    }
    int 树控件项目展开事件(int id, BOOL bExpand, BOOL *pbHandled)
    {
        *pbHandled = TRUE;
        return 0;
    }
};

#define 图像窗口尺寸 960
#define 缩放倍数 0.05
class 图像放大镜类
{
public:
    HWINDOW 放大镜窗口;
    HTuple 图像窗口句柄;
    HObject 图像;
    HTuple 初始位置行, 初始位置列;
    HTuple 当前显示区域行1, 当前显示区域列1, 当前显示区域行2, 当前显示区域列2;
    bool 鼠标左键按下标志 = false;
    bool 鼠标右键按下标志 = false;
    HTuple 图像的高;
    HTuple 图像的宽;
    HTuple 最小显示区域宽度 = 64;    // 设置最小显示区域宽度
    HTuple 最小显示区域高度 = 64;    // 设置最小显示区域高度
    HTuple 最大显示区域宽度 = 65535; // 设置最大显示区域宽度
    HTuple 最大显示区域高度 = 65535; // 设置最大显示区域高度
    图像放大镜类(HWINDOW 父窗口句柄, HObject 输入图像, HTuple 纵坐标, HTuple 横坐标)
    {
        图像 = 输入图像;
        放大镜窗口 = XModalWnd_Create(图像窗口尺寸 + 18, 图像窗口尺寸 + 40, L"放大镜", XWnd_GetHWND(父窗口句柄)); //, window_style_btn_close | window_style_title | window_style_icon | window_style_center | window_style_caption | window_style_border | WS_EX_TOPMOST);
        OpenWindow(31, 9, 图像窗口尺寸, 图像窗口尺寸, (_int64)XWnd_GetHWND(放大镜窗口), "visible", "", &图像窗口句柄);
        SetPart(图像窗口句柄, 纵坐标 - (图像窗口尺寸 / 8), 横坐标 - (图像窗口尺寸 / 8), 纵坐标 + (图像窗口尺寸 / 8), 横坐标 + (图像窗口尺寸 / 8));
        DispObj(图像, 图像窗口句柄);
        GetImageSize(图像, &图像的宽, &图像的高);
        XWnd_RegEventCPP(放大镜窗口, WM_MOUSEWHEEL, &图像放大镜类::鼠标滚动事件);
        XWnd_RegEventCPP(放大镜窗口, WM_LBUTTONDOWN, &图像放大镜类::鼠标左键按下事件);
        XWnd_RegEventCPP(放大镜窗口, WM_MOUSEMOVE, &图像放大镜类::鼠标移动事件);
        XWnd_RegEventCPP(放大镜窗口, WM_LBUTTONUP, &图像放大镜类::鼠标左键抬起事件);
        XWnd_RegEventCPP(放大镜窗口, WM_RBUTTONDOWN, &图像放大镜类::鼠标右键按下事件);
        XWnd_RegEventCPP(放大镜窗口, WM_RBUTTONUP, &图像放大镜类::鼠标右键抬起事件);
        XWnd_RegEventCPP(放大镜窗口, WM_LBUTTONDBLCLK, &图像放大镜类::鼠标左键双击事件);

        int nResult = XModalWnd_DoModal(放大镜窗口);
    }

    ~图像放大镜类()
    {
        CloseWindow(图像窗口句柄);
    }

private:
    int 鼠标滚动事件(UINT nFlags, POINT *pPt, BOOL *pbHandled)
    {
        try
        {
            HTuple 轮数 = GET_WHEEL_DELTA_WPARAM(nFlags);
            GetPart(图像窗口句柄, &当前显示区域行1, &当前显示区域列1, &当前显示区域行2, &当前显示区域列2);
            HTuple 显示区域宽度 = 当前显示区域行2 - 当前显示区域行1;
            HTuple 显示区域高度 = 当前显示区域列2 - 当前显示区域列1;

            HTuple 新显示区域行1, 新显示区域列1, 新显示区域行2, 新显示区域列2;
            if (轮数 > 0)
            {
                新显示区域行1 = 当前显示区域行1 + 显示区域高度 * 缩放倍数;
                新显示区域列1 = 当前显示区域列1 + 显示区域宽度 * 缩放倍数;
                新显示区域行2 = 当前显示区域行2 - 显示区域高度 * 缩放倍数;
                新显示区域列2 = 当前显示区域列2 - 显示区域宽度 * 缩放倍数;
            }
            else
            {
                新显示区域行1 = 当前显示区域行1 - 显示区域高度 * 缩放倍数;
                新显示区域列1 = 当前显示区域列1 - 显示区域宽度 * 缩放倍数;
                新显示区域行2 = 当前显示区域行2 + 显示区域高度 * 缩放倍数;
                新显示区域列2 = 当前显示区域列2 + 显示区域宽度 * 缩放倍数;
            }

            // 限制显示区域大小
            if (新显示区域行2 - 新显示区域行1 < 最小显示区域高度)
            {
                新显示区域行1 = 当前显示区域行1;
                新显示区域行2 = 当前显示区域行2;
            }
            if (新显示区域列2 - 新显示区域列1 < 最小显示区域宽度)
            {
                新显示区域列1 = 当前显示区域列1;
                新显示区域列2 = 当前显示区域列2;
            }
            if (新显示区域行2 - 新显示区域行1 > 最大显示区域高度)
            {
                新显示区域行1 = 当前显示区域行1;
                新显示区域行2 = 当前显示区域行2;
            }
            if (新显示区域列2 - 新显示区域列1 > 最大显示区域宽度)
            {
                新显示区域列1 = 当前显示区域列1;
                新显示区域列2 = 当前显示区域列2;
            }

            SetPart(图像窗口句柄, 新显示区域行1, 新显示区域列1, 新显示区域行2, 新显示区域列2);
            ClearWindow(图像窗口句柄);
            DispObj(图像, 图像窗口句柄);
        }
        catch (HException &HDevExpDefaultException)
        {
        }
        return 0;
    }
    int 鼠标左键按下事件(UINT nFlags, POINT *pPt, BOOL *pbHandled)
    {
        try
        {
            HTuple 按键类型;
            GetMposition(图像窗口句柄, &初始位置行, &初始位置列, &按键类型);
            GetPart(图像窗口句柄, &当前显示区域行1, &当前显示区域列1, &当前显示区域行2, &当前显示区域列2);

            鼠标左键按下标志 = true;
        }
        catch (HException &HDevExpDefaultException)
        {
        }
        return 0;
    }
    int 鼠标右键按下事件(UINT nFlags, POINT *pPt, BOOL *pbHandled)
    {
        鼠标右键按下标志 = true;

        try
        {

            HTuple 纵坐标, 横坐标, 按键类型;
            GetMposition(图像窗口句柄, &纵坐标, &横坐标, &按键类型);
            HTuple 灰度值, 标题栏;
            GetGrayval(图像, 纵坐标, 横坐标, &灰度值);
            switch (灰度值.Length())
            {
            case 2:
                标题栏 = HTuple(u8"纵坐标:") + 纵坐标 + HTuple(u8"\n横坐标:") + 横坐标 + HTuple(u8"\n 灰度:") + 灰度值[0] + HTuple(u8"\n 灰度:") + 灰度值[1];
                break;
            case 3:
                标题栏 = HTuple(u8"纵坐标:") + 纵坐标 + HTuple(u8"\n横坐标:") + 横坐标 + HTuple(u8"\n 灰度R:") + 灰度值[0] + HTuple(u8"\n 灰度G:") + 灰度值[1] + HTuple(u8"\n 灰度B:") + 灰度值[2];
                break;
            default:
                标题栏 = HTuple(u8"纵坐标:") + 纵坐标 + HTuple(u8"\n横坐标:") + 横坐标 + HTuple(u8"\n 灰度:") + 灰度值;
                break;
            }

            HTuple W纵坐标, W横坐标;
            ConvertCoordinatesImageToWindow(图像窗口句柄, 纵坐标, 横坐标 + 8, &W纵坐标, &W横坐标);
            DispText(图像窗口句柄, 标题栏, "window", W纵坐标, W横坐标, "black", HTuple(), HTuple());
        }
        catch (HException &HDevExpDefaultException)
        {
        }
        return 0;
    }
    int 鼠标右键抬起事件(UINT nFlags, POINT *pPt, BOOL *pbHandled)
    {
        鼠标右键按下标志 = false;

        try
        {
            GetPart(图像窗口句柄, &当前显示区域行1, &当前显示区域列1, &当前显示区域行2, &当前显示区域列2);
            ClearWindow(图像窗口句柄);
            SetPart(图像窗口句柄, 当前显示区域行1, 当前显示区域列1, 当前显示区域行2, 当前显示区域列2);
            DispObj(图像, 图像窗口句柄);
        }
        catch (HException &HDevExpDefaultException)
        {
        }
        return 0;
    }
    int 鼠标左键抬起事件(UINT nFlags, POINT *pPt, BOOL *pbHandled)
    {
        鼠标左键按下标志 = false;
        return 0;
    }
    int 鼠标移动事件(UINT nFlags, POINT *pPt, BOOL *pbHandled)
    {
        if (鼠标左键按下标志)
        {

            try
            {
                HTuple 按键类型, 目前位置行, 目前位置列;
                GetMposition(图像窗口句柄, &目前位置行, &目前位置列, &按键类型);
                HTuple 移动行数 = 目前位置行 - 初始位置行;
                HTuple 移动列数 = 目前位置列 - 初始位置列;
                SetPart(图像窗口句柄, 当前显示区域行1 - 移动行数, 当前显示区域列1 - 移动列数, 当前显示区域行2 - 移动行数, 当前显示区域列2 - 移动列数);
                ClearWindow(图像窗口句柄);
                DispObj(图像, 图像窗口句柄);
                GetMposition(图像窗口句柄, &初始位置行, &初始位置列, &按键类型);
                GetPart(图像窗口句柄, &当前显示区域行1, &当前显示区域列1, &当前显示区域行2, &当前显示区域列2);
            }
            // catch (Exception)
            catch (HException &HDevExpDefaultException)
            {
                鼠标左键按下标志 = false;
            }
        }
        else if (鼠标右键按下标志)
        {
            try
            {
                HTuple 纵坐标, 横坐标, 按键类型;
                GetMposition(图像窗口句柄, &纵坐标, &横坐标, &按键类型);
                HTuple 灰度值, 标题栏;
                GetGrayval(图像, 纵坐标, 横坐标, &灰度值);
                switch (灰度值.Length())
                {
                case 2:
                    标题栏 = HTuple(u8"纵坐标:") + 纵坐标 + HTuple(u8"\n横坐标:") + 横坐标 + HTuple(u8"\n 灰度:") + 灰度值[0] + HTuple(u8"\n 灰度:") + 灰度值[1];
                    break;
                case 3:
                    标题栏 = HTuple(u8"纵坐标:") + 纵坐标 + HTuple(u8"\n横坐标:") + 横坐标 + HTuple(u8"\n 灰度R:") + 灰度值[0] + HTuple(u8"\n 灰度G:") + 灰度值[1] + HTuple(u8"\n 灰度B:") + 灰度值[2];
                    break;
                default:
                    标题栏 = HTuple(u8"纵坐标:") + 纵坐标 + HTuple(u8"\n横坐标:") + 横坐标 + HTuple(u8"\n 灰度:") + 灰度值;
                    break;
                }

                HTuple W纵坐标, W横坐标;
                ConvertCoordinatesImageToWindow(图像窗口句柄, 纵坐标, 横坐标 + 8, &W纵坐标, &W横坐标);
                GetPart(图像窗口句柄, &当前显示区域行1, &当前显示区域列1, &当前显示区域行2, &当前显示区域列2);
                ClearWindow(图像窗口句柄);
                SetPart(图像窗口句柄, 当前显示区域行1, 当前显示区域列1, 当前显示区域行2, 当前显示区域列2);
                DispObj(图像, 图像窗口句柄);
                DispText(图像窗口句柄, 标题栏, "window", W纵坐标, W横坐标, "black", HTuple(), HTuple());
            }
            // catch (Exception)
            catch (HException &HDevExpDefaultException)
            {
                鼠标右键按下标志 = false;
            }
        }
        return 0;
    }
    int 鼠标左键双击事件(UINT nFlags, POINT *pPt, BOOL *pbHandled)
    {
        HTuple ROW1;
        HTuple COL1;
        if ((图像的高.L() - 图像的宽.L()) > 0)
        {
            ROW1 = 0;
            COL1 = abs(图像的高.L() - 图像的宽.L()) / 2;
        }
        else
        {
            ROW1 = abs(图像的高.L() - 图像的宽.L()) / 2;
            COL1 = 0;
        }

        SetPart(图像窗口句柄, -ROW1.L(), -COL1.L(), max(图像的高.L(), 图像的宽.L()) - ROW1.L(), max(图像的高.L(), 图像的宽.L()) - COL1.L());
        // SetPart(图像窗口句柄, 0, 0, max(图像的高.L(), 图像的宽.L()), max(图像的高.L(), 图像的宽.L()));
        ClearWindow(图像窗口句柄);
        DispObj(图像, 图像窗口句柄);
        return 0;
    }
};

class 窗口类
{
public:
    HWINDOW m_hWindow;
    int x, y, cx, cy;
    HTuple outQueue;
    QueueHandle inQueue;

    窗口类(HTuple in_x, HTuple in_y, HTuple in_cx, HTuple in_cy, HTuple outQueue_, QueueHandle inQueue_)
        : x(in_x.L()), y(in_y.L()), cx(in_cx.L()), cy(in_cy.L()), outQueue(outQueue_), inQueue(inQueue_)
    {
        Create();
    }

    ~窗口类()
    {
    }

private:
    void Create()
    {
        m_hWindow = XWnd_Create(0, 0, cx, cy, L"YouEyE", NULL, window_style_caption | window_style_border | window_style_center | window_style_icon | window_style_title | window_style_btn_min | window_style_btn_close); // 创建窗口
        XWnd_RegEventCPP(m_hWindow, WM_CLOSE, &窗口类::WM_Close_Event);
        // XWnd_RegEventCPP(m_hWindow, WM_CREATE,&窗口类::WM_CREATE_Event);
    }
    int WM_Close_Event(UINT nFlags, POINT *pPt, BOOL *pbHandled)
    {
        int ret = MessageBoxA(XWnd_GetHWND(m_hWindow), u8"确认关闭吗？", u8"关闭确认", MB_YESNO);
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
    // int WM_CREATE_Event( UINT nFlags, POINT *pPt, BOOL *pbHandled)
    //{

    // return 0;
    //}
};

class 控件类
{
public:
    HWINDOW m_hWindow;
    HELE m_HELE;
    int C_ID, x, y, cx, cy;
    HTuple Pname;
    HTuple outQueue;

    // 基类构造函数（不调用Create，留给派生类决定）
    控件类(HWINDOW in_m_hWindow, HTuple in_C_ID, HTuple in_x, HTuple in_y,
           HTuple in_cx, HTuple in_cy, HTuple in_Pname, HTuple outQueue_)
        : m_hWindow(in_m_hWindow), C_ID(in_C_ID.L()), x(in_x.L()), y(in_y.L()),
          cx(in_cx.L()), cy(in_cy.L()), Pname(in_Pname), outQueue(outQueue_)
    {
    }

    virtual ~控件类() = default; // 虚析构函数必须！

    // 创建虚函数，让派生类必须实现
    virtual void Create() = 0;
};

class 按钮类 : public 控件类
{
public:
    // 派生类手动写构造函数，显式调用Create
    按钮类(HWINDOW in_m_hWindow, HTuple in_C_ID, HTuple in_x, HTuple in_y,
           HTuple in_cx, HTuple in_cy, HTuple in_Pname, HTuple outQueue_)
        : 控件类(in_m_hWindow, in_C_ID, in_x, in_y, in_cx, in_cy, in_Pname, outQueue_)
    {
        Create(); // 关键：在这里调用派生类的Create
    }

protected:
    void Create() override
    {
        m_HELE = XBtn_Create(x, y, cx, cy, Pname.S().TextW(), m_hWindow);
        XEle_RegEventCPP(m_HELE, XE_BNCLICK, &按钮类::OnEventBtnClick);
    }

private:
    int OnEventBtnClick(BOOL *pbHandled) // 按钮点击事件响应
    {
        HTuple hv_Message;
        CreateMessage(&hv_Message);
        SetMessageTuple(hv_Message, "CMD", C_ID);
        EnqueueMessage(outQueue, hv_Message, HTuple(), HTuple());
        ClearMessage(hv_Message);
        return 0; // 事件的返回值
    }
};

class 参数类
{
public:
    HWINDOW m_hWindow;
    HELE 参数值容器;
    int C_ID, x, y, cx, cy;
    HTuple Pname;
    HTuple outQueue;
    HTuple 默认值;
    HTuple 数据类型;
    HTuple 参数限制;
    // 派生类手动写构造函数，显式调用Create
    参数类(HWINDOW in_m_hWindow, HTuple in_C_ID, HTuple in_x, HTuple in_y,
           HTuple in_cx, HTuple in_cy, HTuple in_Pname, HTuple outQueue_, HTuple 默认值_, HTuple 数据类型_, HTuple 限制_)
    {
        m_hWindow = in_m_hWindow;
        C_ID = in_C_ID.L();
        x = in_x.L();
        y = in_y.L();
        cx = in_cx.L();
        cy = in_cy.L();
        Pname = in_Pname;
        outQueue = outQueue_;
        默认值 = 默认值_;
        数据类型 = 数据类型_;
        参数限制 = 限制_;
        Create(); // 关键：在这里调用派生类的Create
    }

private:
    void Create()
    {
        HELE m_Tite = XEdit_Create(x, y, 60, cy, m_hWindow);
        XEdit_SetText(m_Tite, Pname.S().TextW());
        XEdit_EnableReadOnly(m_Tite, true);
        HELE m_hButton = XBtn_Create(x + cx - 30, y, 30, cy, L"应用", m_hWindow);
        参数值容器 = XEdit_Create(x + 61, y, cx - 61 - 29, cy, m_hWindow);

        if (数据类型.S() == "string")
        {
            XEdit_SetText(参数值容器, 默认值.S().TextW());
        }
        else if (数据类型.S() == "int")
        {
            HTuple 参数值I;
            TupleString(默认值, ".0f", &参数值I);
            XEdit_SetText(参数值容器, 参数值I.S().TextW());
        }
        else if (数据类型.S() == "double")
        {
            HTuple 参数值D;
            TupleString(默认值, ".3f", &参数值D);
            XEdit_SetText(参数值容器, 参数值D.S().TextW());
        }

        XEle_RegEventCPP(m_hButton, XE_BNCLICK, &参数类::OnEventBtnClick);
    }

    int OnEventBtnClick(BOOL *pbHandled) // 按钮点击事件响应
    {
        HTuple hv_Message;
        CreateMessage(&hv_Message);
        SetMessageTuple(hv_Message, "CMD", C_ID);
        wchar_t pOut[32];
        int len = XEdit_GetText(参数值容器, pOut, 32);
        if (数据类型.S() == "string")
        {
            HTuple hv_TupleVChar(pOut);
            SetMessageTuple(hv_Message, "Data", hv_TupleVChar);
        }
        else if (数据类型.S() == "int")
        {
            HTuple hv_TupleV;
            HTuple hv_TupleVChar(pOut);
            TupleNumber(hv_TupleVChar, &hv_TupleV);
            if (hv_TupleV.L() < 参数限制[0].L() || hv_TupleV.L() > 参数限制[1].L())
            {
                HTuple 参数值I;
                TupleString(默认值, ".0f", &参数值I);
                XEdit_SetText(参数值容器, 参数值I.S().TextW());

                int ret = MessageBox(NULL, u8"参数不合规", u8"参数显示", MB_YESNO);
                return 0;
            }
            默认值 = hv_TupleV;
            SetMessageTuple(hv_Message, "Data", hv_TupleV);
        }
        else if (数据类型.S() == "double")
        {
            HTuple hv_TupleV;
            HTuple hv_TupleVChar(pOut);
            TupleNumber(hv_TupleVChar, &hv_TupleV);
            if (hv_TupleV.L() < 参数限制[0].L() || hv_TupleV.L() > 参数限制[1].L())
            {

                HTuple 参数值D;
                TupleString(默认值, ".3f", &参数值D);
                XEdit_SetText(参数值容器, 参数值D.S().TextW());
                int ret = MessageBox(NULL, u8"参数不合规", u8"参数显示", MB_YESNO);
                return 0;
            }
            默认值 = hv_TupleV;
            SetMessageTuple(hv_Message, "Data", hv_TupleV);
        }

        EnqueueMessage(outQueue, hv_Message, HTuple(), HTuple());
        ClearMessage(hv_Message);
        return 0; // 事件的返回值
    }
};

class 图片显示控件
{
public:
    HWINDOW m_hWindow;
    int C_ID, x, y, cx, cy;
    HTuple Pname;
    HTuple hv_mode;
    HTuple outQueue;
    HWindow HwindowsHandle;

    // 派生类手动写构造函数，显式调用Create
    图片显示控件(HWINDOW in_m_hWindow, HTuple in_C_ID, HTuple in_x, HTuple in_y,
           HTuple in_cx, HTuple in_cy, HTuple in_Pname,HTuple hv_mode_,HTuple outQueue_, HWindow HwindowsHandle_)
    {
        m_hWindow = in_m_hWindow;
        C_ID = in_C_ID.L();
        x = in_x.L();
        y = in_y.L();
        cx = in_cx.L();
        cy = in_cy.L();
        Pname = in_Pname+in_C_ID;
        hv_mode=hv_mode_;
        outQueue = outQueue_;
        HwindowsHandle=HwindowsHandle_;
        Create(); // 关键：在这里调用派生类的Create
    }

private:
    void Create()
    {
        
    }
};