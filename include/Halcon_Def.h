#include "Halcon_UI.h"
#include "HalconCpp.h"
#include "xcgui.h"
#include <windows.h>
#include <vector>
#include "StringQueue.h"
using namespace HalconCpp;

static HTuple inDict;
static HTuple outQueue;
static QueueHandle inQueue;

#define DEFHalconForPr                                          \
    HTuple hv_GenParamValue, hv_Index;                          \
    HTuple hv_ControlHandle, hv_ControlType, hv_ID, hv_X, hv_Y; \
    HTuple hv_CX, hv_CY, hv_pName, hv_Text;                     \
    GetDictParam(inDict, "keys", HTuple(), &hv_GenParamValue);  \
    HTuple end_val8 = hv_GenParamValue.TupleLength() - 1;       \
    HTuple step_val8 = 1;                                       \
    for (hv_Index = 0; hv_Index.Continue(end_val8, step_val8); hv_Index += step_val8)

#define GetDictTuple_hv_ID_hv_X_hv_Y_hv_CX_hv_CY_hv_pName_hv_Text \
    GetDictTuple(hv_ControlHandle, u8"控件ID", &hv_ID);           \
    GetDictTuple(hv_ControlHandle, "X", &hv_X);                   \
    GetDictTuple(hv_ControlHandle, "Y", &hv_Y);                   \
    GetDictTuple(hv_ControlHandle, "CX", &hv_CX);                 \
    GetDictTuple(hv_ControlHandle, "CY", &hv_CY);                 \
    GetDictTuple(hv_ControlHandle, u8"标题", &hv_pName);          \
    GetDictTuple(hv_ControlHandle, u8"默认内容", &hv_Text);

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

class 控件类
{
public:
    HWINDOW m_hWindow;
    HELE m_hButton;
    int C_ID, x, y, cx, cy;
    HTuple Pname;

    // 基类构造函数（不调用Create，留给派生类决定）
    控件类(HWINDOW in_m_hWindow, HTuple in_C_ID, HTuple in_x, HTuple in_y,
           HTuple in_cx, HTuple in_cy, HTuple in_Pname)
        : m_hWindow(in_m_hWindow), C_ID(in_C_ID.L()), x(in_x.L()), y(in_y.L()),
          cx(in_cx.L()), cy(in_cy.L()), Pname(in_Pname)
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
           HTuple in_cx, HTuple in_cy, HTuple in_Pname)
        : 控件类(in_m_hWindow, in_C_ID, in_x, in_y, in_cx, in_cy, in_Pname)
    {
        Create(); // 关键：在这里调用派生类的Create
    }

protected:
    void Create() override
    {
        m_hButton = XBtn_Create(x, y, cx, cy, XC_utf8tow(Pname.S()), m_hWindow);
        XEle_RegEventCPP(m_hButton, XE_BNCLICK, &按钮类::OnEventBtnClick);
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