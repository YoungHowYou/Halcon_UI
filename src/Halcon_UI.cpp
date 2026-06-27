#include "Halcon_UI.h"
#include "websocket.h"

#include "HalconCpp.h"
using namespace HalconCpp;

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define H_WebServer_TAG 0xC0FFEE50 
#define H_WebServer_SEM_TYPE "WebServer"

extern "C"
{
    typedef struct
    {
        int server_id;
        bool closed;
    } HWebServerHandleData;

    static Herror HWebServerHandleDestructor(Hproc_handle ph, HWebServerHandleData *data)
    {
        if (!data->closed && data->server_id >= 0)
        {
            CloseWebServer(data->server_id);
            data->closed = true;
        }
        return HFree(ph, data);
    }
    // 句柄类型描述符
    const HHandleInfo HandleTypeWebServer = HANDLE_INFO_INITIALIZER_NOSER(H_WebServer_TAG, H_WebServer_SEM_TYPE, HWebServerHandleDestructor, NULL, NULL);
}

// ==================== JPEG 编码（跨平台 / libjpeg-turbo）====================
// 使用 libjpeg-turbo 的 TurboJPEG API，Windows & Linux 统一路径
#include <turbojpeg.h>

// 将 Halcon planar 图像编码为 JPEG
// rPtr: R通道(灰度时为唯一通道), gPtr/bPtr: G/B通道(灰度时为NULL)
// 返回 malloc 的 JPEG buffer，调用者负责 free
static char* EncodeJpeg(const unsigned char* rPtr, const unsigned char* gPtr, const unsigned char* bPtr,
                        int w, int h, int channels, int quality, size_t* outSize)
{
    *outSize = 0;

    tjhandle tj = tjInitCompress();
    if (!tj) return nullptr;

    // Planar → interleaved (libjpeg-turbo 需要 interleaved 输入)
    int pixel_format = (channels == 1) ? TJPF_GRAY : TJPF_RGB;
    int row_bytes = w * channels;
    unsigned char* row = (unsigned char*)malloc((size_t)row_bytes);
    if (!row) { tjDestroy(tj); return nullptr; }

    // 逐行压缩
    unsigned char* jpegBuf = nullptr;
    unsigned long  jpegSize = 0;

    // 准备 interleaved 缓冲区（一整帧）
    size_t frame_size = (size_t)w * h * channels;
    unsigned char* interleaved = (unsigned char*)malloc(frame_size);
    if (!interleaved) { free(row); tjDestroy(tj); return nullptr; }

    if (channels == 1) {
        memcpy(interleaved, rPtr, frame_size);
    } else {
        for (int y = 0; y < h; y++) {
            const unsigned char* sr = rPtr + (size_t)y * w;
            const unsigned char* sg = gPtr + (size_t)y * w;
            const unsigned char* sb = bPtr + (size_t)y * w;
            unsigned char* dst = interleaved + (size_t)y * w * 3;
            for (int x = 0; x < w; x++) {
                dst[x * 3]     = sr[x];
                dst[x * 3 + 1] = sg[x];
                dst[x * 3 + 2] = sb[x];
            }
        }
    }

    int ret = tjCompress2(tj, interleaved, w, 0, h, pixel_format,
                          &jpegBuf, &jpegSize,
                          (channels == 1) ? TJSAMP_GRAY : TJSAMP_420,
                          quality, TJFLAG_FASTDCT);

    free(interleaved);
    free(row);
    tjDestroy(tj);

    if (ret != 0 || !jpegBuf) return nullptr;

    // tjCompress2 使用 tjAlloc，复制一份让调用方统一用 free()
    char* result = (char*)malloc(jpegSize);
    if (result) {
        memcpy(result, jpegBuf, jpegSize);
        *outSize = (size_t)jpegSize;
    }
    tjFree(jpegBuf);
    return result;
}

// ==================== JSON 协议校验 ====================
// 按 PROTOCOL.md 规范验证 JSON 帧格式
// 返回值: H_MSG_TRUE 通过 / 1099x 格式错误码
#define ERR_JSON_NO_CMD      10990  // JSON 缺少 "CMD" 字段
#define ERR_JSON_NO_DATA     10991  // JSON 缺少 "Data" 字段
#define ERR_JSON_CMD_TYPE    10992  // CMD 类型错误（非整数）
#define ERR_JSON_MISSING_KEY 10993  // Data 缺少必要字段
#define ERR_JSON_VALUE_TYPE  10994  // 字段值类型错误

static Herror ValidateJsonProtocol(const HTuple& dict_json)
{
    // 1. 必须有 "CMD"
    HTuple CMD;
    try {
        GetDictTuple(dict_json, "CMD", &CMD);
    } catch (...) {
        return ERR_JSON_NO_CMD;
    }
    // 确保 CMD 可转为整数
    Hlong cmdVal;
    try {
        cmdVal = CMD.L();
    } catch (...) {
        return ERR_JSON_CMD_TYPE;
    }
    if (cmdVal < 0 || cmdVal > 999) {
        return ERR_JSON_CMD_TYPE;
    }

    // 2. 必须有 "Data"
    HTuple Data;
    try {
        GetDictTuple(dict_json, "Data", &Data);
    } catch (...) {
        return ERR_JSON_NO_DATA;
    }

    // 3. 按 CMD 校验 Data 必要字段
    switch (cmdVal) {
    case 0: {
        // 图像帧: Data 必须包含 宽/高/图号/通道（全部为数值）
        const char* required[] = { u8"宽", u8"高", u8"图号", u8"通道" };
        for (int i = 0; i < 4; i++) {
            HTuple tmp;
            try { GetDictTuple(Data, required[i], &tmp); }
            catch (...) { return ERR_JSON_MISSING_KEY; }
            try { tmp.L(); }
            catch (...) { return ERR_JSON_VALUE_TYPE; }
        }
        break;
    }
    default:
        // 其他 CMD 不做字段级校验，只要 Data 是 dict 即可
        break;
    }

    return H_MSG_TRUE;
}

// ==================== 创建 HTTP 服务器 ====================
Herror HCreateWebServer(Hproc_handle proc_handle)
{
    HAllocStringMem(proc_handle, 64);
    Hcpar port;
    HGetSPar(proc_handle, 1, LONG_PAR, &port, 1);

    Hcpar web_root;
    HGetSPar(proc_handle, 2, STRING_PAR, &web_root, 1);

    const char* root_str = web_root.par.s;
    int ret = CreateWebServer((uint16_t)port.par.l, root_str);
    if (ret < 0) return 10000 - ret;

    HWebServerHandleData **handle_data;
    // 分配输出句柄
    HCkP(HAllocOutputHandle(proc_handle, 1, &handle_data, &HandleTypeWebServer));
    // 分配并初始化用户数据
    HCkP(HAlloc(proc_handle, sizeof(HWebServerHandleData), (void **)handle_data));
    (*handle_data)->server_id = ret;
    (*handle_data)->closed = false;
    return H_MSG_TRUE;
}

// ==================== 接收数据 ====================
Herror HRecvWebData(Hproc_handle proc_handle)
{
    HWebServerHandleData *handle_data;
    HGetCElemH1(proc_handle, 1, &HandleTypeWebServer, &handle_data);

    Hcpar timeout_ms;
    HGetSPar(proc_handle, 2, LONG_PAR, &timeout_ms, 1);

    const Hcpar *cdict = nullptr;
    INT4_8 num;
    HGetPPar(proc_handle, 3, &cdict, &num);
    HTuple hv_DictHandle(const_cast<Hcpar*>(cdict), 1);

    char *jsontext = nullptr;
    char *data = nullptr;
    size_t out_length = 0;

    int ret = RecvWebData(handle_data->server_id, &jsontext, &data, &out_length, (int)timeout_ms.par.l);
    if (ret != 0) return 10000 - ret;

    HTuple h_jsontext(jsontext);
    HTuple dict_json;
    
    JsonToDict(h_jsontext, HTuple(), HTuple(), &dict_json);

    // 协议格式校验
    Herror vret = ValidateJsonProtocol(dict_json);
    if (vret != H_MSG_TRUE) { free(jsontext); free(data); return vret; }

    HTuple CMD;
    GetDictTuple(dict_json, "CMD", &CMD);

    if (CMD.L() == 0)
    {
        HTuple Data;
        GetDictTuple(dict_json, "Data", &Data);

        HTuple 宽, 高, 图号, 通道;
        GetDictTuple(Data, u8"宽", &宽);
        GetDictTuple(Data, u8"高", &高);
        GetDictTuple(Data, u8"图号", &图号);
        GetDictTuple(Data, u8"通道", &通道);

        SetDictTuple(hv_DictHandle, u8"命令", dict_json);

        HObject Image;
        if (通道.L() == 1)
        {
            GenImage1(&Image, "byte", 宽.L(), 高.L(), (int64_t)data);
          
        }
        else
        {
            int64_t Tw = 宽.L() * 高.L() ;
            HObject ImageR, ImageG, ImageB; 
          
            GenImage1(&ImageR, "byte", 宽.L(), 高.L(), (int64_t)data);
            GenImage1(&ImageG, "byte", 宽.L(), 高.L(), (int64_t)(data + Tw));
            GenImage1(&ImageB, "byte", 宽.L(), 高.L(), (int64_t)(data + Tw * 2));
          
            Compose3(ImageR, ImageG, ImageB, &Image);
        }
        SetDictObject(Image, hv_DictHandle, u8"图");
    }
    else
    {
        SetDictTuple(hv_DictHandle, u8"命令", dict_json);
    }

    free(jsontext);
    free(data);
    return H_MSG_TRUE;
}

// ==================== 发送数据（图像自动 JPEG 压缩）====================
Herror HSendWebData(Hproc_handle proc_handle)
{
    HWebServerHandleData *handle_data;
    HGetCElemH1(proc_handle, 1, &HandleTypeWebServer, &handle_data);

    const Hcpar *cdict = nullptr;
    INT4_8 num;
    HGetPPar(proc_handle, 2, &cdict, &num);

    HTuple hv_DictHandle(const_cast<Hcpar*>(cdict), 1);
    HTuple dict_json;
    GetDictTuple(hv_DictHandle, u8"命令", &dict_json);

    // 协议格式校验
    Herror vret = ValidateJsonProtocol(dict_json);
    if (vret != H_MSG_TRUE) return vret;

    int ret = -1;

    // 检查是否附带图像对象 → 有就 JPEG 编码发送，没有就只发 JSON
    HObject Image;
    bool hasImage = false;
    try {
        GetDictObject(&Image, hv_DictHandle, u8"图");
        hasImage = true;
    } catch (...) {
        hasImage = false;
    }

    if (hasImage)
    {
        HTuple Data;
        GetDictTuple(dict_json, "Data", &Data);

        HTuple 宽, 高, TYPE位深, 通道;
        GetDictTuple(Data, u8"通道", &通道);
        GetDictTuple(Data, u8"宽", &宽);
        GetDictTuple(Data, u8"高", &高);

        // 读取用户指定的编码格式，默认 jpeg
        HTuple fmt;
        bool hasFmt = false;
        try { GetDictTuple(Data, "fmt", &fmt); hasFmt = true; } catch (...) {}
        const char* fmtStr = hasFmt ? fmt.S() : "jpeg";

        HTuple Text_json;
        DictToJson(dict_json, HTuple(), HTuple(), &Text_json);

        if (strcmp(fmtStr, "raw") == 0)
        {
            // RAW planar 像素直接发送
            HTuple ptr;
            size_t rawSize = 0;
            char* rawData = nullptr;

            if (通道.L() == 1)
            {
                GetImagePointer1(Image, &ptr, &TYPE位深, &宽, &高);
                rawSize = (size_t)宽.L() * 高.L();
                rawData = (char*)malloc(rawSize);
                if (rawData) memcpy(rawData, (void*)ptr.L(), rawSize);
            }
            else if (通道.L() == 3)
            {
                HTuple ptrR, ptrG, ptrB;
                GetImagePointer3(Image, &ptrR, &ptrG, &ptrB, &TYPE位深, &宽, &高);
                size_t planeSize = (size_t)宽.L() * 高.L();
                rawSize = planeSize * 3;
                rawData = (char*)malloc(rawSize);
                if (rawData) {
                    memcpy(rawData, (void*)ptrR.L(), planeSize);
                    memcpy(rawData + planeSize, (void*)ptrG.L(), planeSize);
                    memcpy(rawData + planeSize * 2, (void*)ptrB.L(), planeSize);
                }
            }

            if (rawData)
            {
                ret = SendWebData(handle_data->server_id, Text_json.S(), rawData, rawSize);
                free(rawData);
            }
        }
        else
        {
            // JPEG 编码发送
            if (!hasFmt) SetDictTuple(Data, "fmt", HTuple("jpeg"));
            DictToJson(dict_json, HTuple(), HTuple(), &Text_json);

            size_t jpegSize = 0;
            char* jpegData = nullptr;

            if (通道.L() == 1)
            {
                HTuple ptr;
                GetImagePointer1(Image, &ptr, &TYPE位深, &宽, &高);
                jpegData = EncodeJpeg((const unsigned char*)ptr.L(), nullptr, nullptr,
                                      (int)宽.L(), (int)高.L(), 1, 80, &jpegSize);
            }
            else if (通道.L() == 3)
            {
                HTuple ptrR, ptrG, ptrB;
                GetImagePointer3(Image, &ptrR, &ptrG, &ptrB, &TYPE位深, &宽, &高);
                jpegData = EncodeJpeg((const unsigned char*)ptrR.L(),
                                      (const unsigned char*)ptrG.L(),
                                      (const unsigned char*)ptrB.L(),
                                      (int)宽.L(), (int)高.L(), 3, 80, &jpegSize);
            }

            if (jpegData)
            {
                ret = SendWebData(handle_data->server_id, Text_json.S(), jpegData, jpegSize);
                free(jpegData);
            }
        }
    }
    else
    {
        // 纯 JSON，无图像
        HTuple Text_json;
        DictToJson(dict_json, HTuple(), HTuple(), &Text_json);
        ret = SendWebData(handle_data->server_id, Text_json.S(), nullptr, 0);
    }

    if (ret != 0) return 10000 - ret;

    return H_MSG_TRUE;
}

// ==================== 关闭服务器 ====================
Herror HCloseWebServer(Hproc_handle proc_handle)
{
    HWebServerHandleData *handle_data;
    HGetCElemH1(proc_handle, 1, &HandleTypeWebServer, &handle_data);

    if (!handle_data->closed && handle_data->server_id >= 0)
    {
        CloseWebServer(handle_data->server_id);
        handle_data->closed = true;
    }
    return H_MSG_TRUE;
}
