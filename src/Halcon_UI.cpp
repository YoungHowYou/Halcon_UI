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
    if (ret != 0) return 10000 + (-ret);

    HTuple h_jsontext(jsontext);
    HTuple dict_json;
    JsonToDict(h_jsontext, HTuple(), HTuple(), &dict_json);

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

    HTuple CMD;
    GetDictTuple(dict_json, "CMD", &CMD);

    int ret = -1;

    if (CMD.L() == 0) // 图像数据 → JPEG 压缩后发送
    {
        HObject Image;
        GetDictObject(&Image, hv_DictHandle, u8"图");

        HTuple Data;
        GetDictTuple(dict_json, "Data", &Data);

        HTuple 宽, 高, TYPE位深, 图号, 通道;
        GetDictTuple(Data, u8"通道", &通道);
        GetDictTuple(Data, u8"图号", &图号);

        // 在 Data 字典中标记编码格式，前端据此解码
        SetDictTuple(Data, "fmt", HTuple("jpeg"));

        // 重新序列化 JSON（现在包含 fmt 字段）
        HTuple Text_json;
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

        if (ret != 0) return 10000 + (-ret);
    }
    else
    {
        // 非图像命令，只发 JSON
        HTuple Text_json;
        DictToJson(dict_json, HTuple(), HTuple(), &Text_json);
        ret = SendWebData(handle_data->server_id, Text_json.S(), nullptr, 0);
        if (ret != 0) return 10000 + (-ret);
    }

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
