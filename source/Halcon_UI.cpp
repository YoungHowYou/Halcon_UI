#include "Halcon_UI.h"
#include "websocket.h"

#include "HalconCpp.h"
using namespace HalconCpp;

// ==================== GDI+ JPEG 编码 ====================
#include <objbase.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

static ULONG_PTR g_gdip_token = 0;
static bool g_gdip_init = false;

static void EnsureGdiPlus() {
    if (!g_gdip_init) {
        Gdiplus::GdiplusStartupInput si;
        Gdiplus::GdiplusStartup(&g_gdip_token, &si, NULL);
        g_gdip_init = true;
    }
}

// 缓存 JPEG Encoder CLSID（只查一次）
static CLSID g_jpeg_clsid = {0};
static bool g_jpeg_clsid_found = false;

static int GetJpegClsid(CLSID* pClsid) {
    if (g_jpeg_clsid_found) {
        *pClsid = g_jpeg_clsid;
        return 0;
    }
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    Gdiplus::ImageCodecInfo* info = (Gdiplus::ImageCodecInfo*)malloc(size);
    Gdiplus::GetImageEncoders(num, size, info);
    for (UINT i = 0; i < num; i++) {
        if (wcscmp(info[i].MimeType, L"image/jpeg") == 0) {
            *pClsid = info[i].Clsid;
            g_jpeg_clsid = info[i].Clsid;
            g_jpeg_clsid_found = true;
            free(info);
            return 0;
        }
    }
    free(info);
    return -1;
}

// 将 Halcon planar 图像编码为 JPEG
// rPtr: R通道(灰度时为唯一通道), gPtr/bPtr: G/B通道(灰度时为NULL)
// 返回 malloc 的 JPEG buffer，调用者负责 free
static char* EncodeJpeg(const unsigned char* rPtr, const unsigned char* gPtr, const unsigned char* bPtr,
                        int w, int h, int channels, int quality, size_t* outSize)
{
    EnsureGdiPlus();
    *outSize = 0;

    // Halcon planar → GDI+ interleaved BGR
    int stride = ((w * 3 + 3) & ~3); // 4字节对齐
    unsigned char* bgr = (unsigned char*)malloc((size_t)stride * h);
    if (!bgr) return nullptr;

    if (channels == 1) {
        for (int y = 0; y < h; y++) {
            const unsigned char* src = rPtr + y * w;
            unsigned char* dst = bgr + y * stride;
            for (int x = 0; x < w; x++) {
                unsigned char v = src[x];
                dst[x * 3] = dst[x * 3 + 1] = dst[x * 3 + 2] = v;
            }
        }
    } else {
        for (int y = 0; y < h; y++) {
            const unsigned char* sr = rPtr + y * w;
            const unsigned char* sg = gPtr + y * w;
            const unsigned char* sb = bPtr + y * w;
            unsigned char* dst = bgr + y * stride;
            for (int x = 0; x < w; x++) {
                dst[x * 3]     = sb[x];
                dst[x * 3 + 1] = sg[x];
                dst[x * 3 + 2] = sr[x];
            }
        }
    }

    // 创建 Bitmap 并编码
    Gdiplus::Bitmap bmp(w, h, stride, PixelFormat24bppRGB, bgr);

    CLSID jpegClsid;
    if (GetJpegClsid(&jpegClsid) < 0) { free(bgr); return nullptr; }

    Gdiplus::EncoderParameters params;
    params.Count = 1;
    params.Parameter[0].Guid = Gdiplus::EncoderQuality;
    params.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
    params.Parameter[0].NumberOfValues = 1;
    ULONG q = (ULONG)quality;
    params.Parameter[0].Value = &q;

    IStream* stream = NULL;
    CreateStreamOnHGlobal(NULL, TRUE, &stream);
    Gdiplus::Status st = bmp.Save(stream, &jpegClsid, &params);
    free(bgr);

    if (st != Gdiplus::Ok) { stream->Release(); return nullptr; }

    STATSTG stat;
    stream->Stat(&stat, STATFLAG_DEFAULT);
    size_t sz = (size_t)stat.cbSize.LowPart;

    char* jpeg = (char*)malloc(sz);
    if (jpeg) {
        LARGE_INTEGER pos;
        pos.QuadPart = 0;
        stream->Seek(pos, STREAM_SEEK_SET, NULL);
        ULONG bytesRead = 0;
        stream->Read(jpeg, (ULONG)sz, &bytesRead);
        *outSize = (size_t)bytesRead;
    }
    stream->Release();
    return jpeg;
}

// ==================== 创建 HTTP 服务器 ====================
Herror HCreateWebServer(Hproc_handle proc_handle)
{
    Hcpar port;
    HGetSPar(proc_handle, 1, LONG_PAR, &port, 1);

    int ret = CreateWebServer((uint16_t)port.par.l);
    if (ret < 0) return 10000 - ret;

    int64_t server_id = (int64_t)ret;
    HPutElem(proc_handle, 1, &server_id, 1, LONG_PAR);
    return H_MSG_TRUE;
}

// ==================== 接收数据 ====================
Herror HRecvWebData(Hproc_handle proc_handle)
{
    Hcpar server_id;
    Hcpar timeout_ms;
    HGetSPar(proc_handle, 1, LONG_PAR, &server_id, 1);
    HGetSPar(proc_handle, 2, LONG_PAR, &timeout_ms, 1);

    Hcpar *dict;
    INT4_8 num;
    HGetPPar(proc_handle, 3, &dict, &num);
    HTuple hv_DictHandle(dict, 1);

    char *jsontext = nullptr;
    char *data = nullptr;
    size_t out_length = 0;

    int ret = RecvWebData(server_id.par.l, &jsontext, &data, &out_length, timeout_ms.par.l);
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

        HTuple 宽, 高, 位深, 通道;
        GetDictTuple(Data, u8"宽", &宽);
        GetDictTuple(Data, u8"高", &高);
        GetDictTuple(Data, u8"位深", &位深);
        GetDictTuple(Data, u8"通道", &通道);

        SetDictTuple(hv_DictHandle, u8"命令", dict_json);

        HObject Image;
        if (通道.L() == 1)
        {
            if (位深.L() == 1)
                GenImage1(&Image, "byte", 宽.L(), 高.L(), (__int64)data);
            else
                GenImage1(&Image, "uint2", 宽.L(), 高.L(), (__int64)data);
        }
        else
        {
            int64_t Tw = 宽.L() * 高.L() * 位深.L();
            HObject ImageR, ImageG, ImageB;
            if (位深.L() == 1)
            {
                GenImage1(&ImageR, "byte", 宽.L(), 高.L(), (__int64)data);
                GenImage1(&ImageG, "byte", 宽.L(), 高.L(), (__int64)(data + Tw));
                GenImage1(&ImageB, "byte", 宽.L(), 高.L(), (__int64)(data + Tw * 2));
            }
            else
            {
                GenImage1(&ImageR, "uint2", 宽.L(), 高.L(), (__int64)data);
                GenImage1(&ImageG, "uint2", 宽.L(), 高.L(), (__int64)(data + Tw));
                GenImage1(&ImageB, "uint2", 宽.L(), 高.L(), (__int64)(data + Tw * 2));
            }
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
    Hcpar server_id;
    HGetSPar(proc_handle, 1, LONG_PAR, &server_id, 1);

    Hcpar *dict;
    INT4_8 num;
    HGetPPar(proc_handle, 2, &dict, &num);

    HTuple hv_DictHandle(dict, 1);
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

        HTuple 宽, 高, TYPE位深, 位深, 通道;
        GetDictTuple(Data, u8"通道", &通道);
        GetDictTuple(Data, u8"位深", &位深);

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
            ret = SendWebData(server_id.par.l, Text_json.S(), jpegData, jpegSize);
            free(jpegData);
        }

        if (ret != 0) return 10000 + (-ret);
    }
    else
    {
        // 非图像命令，只发 JSON
        HTuple Text_json;
        DictToJson(dict_json, HTuple(), HTuple(), &Text_json);
        ret = SendWebData(server_id.par.l, Text_json.S(), nullptr, 0);
        if (ret != 0) return 10000 + (-ret);
    }

    return H_MSG_TRUE;
}

// ==================== 关闭服务器 ====================
Herror HCloseWebServer(Hproc_handle proc_handle)
{
    Hcpar server_id;
    HGetSPar(proc_handle, 1, LONG_PAR, &server_id, 1);
    CloseWebServer((int)server_id.par.l);
    return H_MSG_TRUE;
}
