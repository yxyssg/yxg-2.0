/* ============================================================
 * video_loader.c — YXG 视频播放运行时 (Windows Media Foundation)
 *
 * 视频: MP4/H.264 等系统解码器支持的全部格式
 * 音频: 预解码为 PCM S16, 交给 miniaudio 内存音源播放
 *
 * 编译: gcc -m32 -O2 -c video_loader.c -o video_loader.obj
 * 链接: ... video_loader.obj music_loader.obj
 *         -lmfplat -lmfreadwrite -lmfuuid -lole32 -luuid -lwinmm
 * ============================================================ */

#include <windows.h>
#include <objbase.h>
#include <mfapi.h>
#include <mfobjects.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <propsys.h>

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#define MAX_VIDEO 16

/* FOURCC (GUID.Data1 小端) */
#define FOURCC_NV12  0x3231564Eu
#define FOURCC_YUY2  0x32595559u
#define FOURCC_I420  0x30323449u
#define FOURCC_IYUV  0x56555949u
#define FOURCC_YV12  0x32315659u
#define FOURCC_RGB32 22u

/* miniaudio (music_loader.c) 导出 */
extern int  audio_load_memory(const unsigned char* data, int bytes,
                              int sample_rate, int channels);
extern void audio_play(int handle, int loop);
extern void audio_stop(int handle);
extern void audio_unload(int handle);

void video_rewind(int handle);

typedef struct {
    IMFSourceReader* reader;
    unsigned char*   frame;      /* 当前帧 BGRA, 自底向上(DIB 序), w*h*4 */
    int              width;
    int              height;
    DWORD            video_idx;
    double           duration;
    double           cur_time;
    UINT32           subtype;
    int              is_rgb32;
    int              in_use;
    int              loop;
    int              got_frame;
    int              audio_handle;  /* miniaudio 句柄, 0=无 */
    unsigned char*   pcm_data;      /* 预解码 PCM, 生命周期与视频一致 */
    int              pcm_len;
} VideoSlot;

static VideoSlot slots[MAX_VIDEO];
static int mf_started = 0;
static int g_last_error = 0;
static UINT32 g_last_subtype = 0;
static int g_last_hresult = 0;

int video_last_error(void) { int e = g_last_error; g_last_error = 0; return e; }
int video_dbg_subtype(void) { return (int)g_last_subtype; }
int video_last_hresult(void) { return g_last_hresult; }

static VideoSlot* slot_of(int handle) {
    int idx = handle - 1;
    if (idx < 0 || idx >= MAX_VIDEO) return NULL;
    if (!slots[idx].in_use || !slots[idx].reader) return NULL;
    return &slots[idx];
}

static int mf_init(void) {
    if (mf_started) return 1;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    if (FAILED(hr)) { g_last_error = 1; return 0; }
    mf_started = 1;
    return 1;
}

static int clamp8(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return v;
}

/* 下面三个转换都把结果写成"自底向上"(DIB 序) */
static void nv12_to_bgra(const uint8_t* Y, int ystride,
                         const uint8_t* UV, int uvstride,
                         uint8_t* dst, int w, int h) {
    for (int y = 0; y < h; y++) {
        const uint8_t* yrow = Y + (size_t)y * (size_t)ystride;
        const uint8_t* uvrow = UV + (size_t)(y >> 1) * (size_t)uvstride;
        uint8_t* drow = dst + (size_t)(h - 1 - y) * (size_t)w * 4;
        for (int x = 0; x < w; x++) {
            int C = yrow[x] - 16;
            int D = uvrow[(x & ~1)] - 128;
            int E = uvrow[(x & ~1) + 1] - 128;
            drow[x*4+0] = (uint8_t)clamp8((298 * C + 516 * D + 128) >> 8);
            drow[x*4+1] = (uint8_t)clamp8((298 * C - 100 * D - 208 * E + 128) >> 8);
            drow[x*4+2] = (uint8_t)clamp8((298 * C + 409 * E + 128) >> 8);
            drow[x*4+3] = 0xFF;
        }
    }
}

static void i420_to_bgra(const uint8_t* Y, int ystride,
                         const uint8_t* U, const uint8_t* V, int uvstride,
                         uint8_t* dst, int w, int h) {
    for (int y = 0; y < h; y++) {
        const uint8_t* yrow = Y + (size_t)y * (size_t)ystride;
        const uint8_t* urow = U + (size_t)(y >> 1) * (size_t)uvstride;
        const uint8_t* vrow = V + (size_t)(y >> 1) * (size_t)uvstride;
        uint8_t* drow = dst + (size_t)(h - 1 - y) * (size_t)w * 4;
        for (int x = 0; x < w; x++) {
            int C = yrow[x] - 16;
            int D = urow[x >> 1] - 128;
            int E = vrow[x >> 1] - 128;
            drow[x*4+0] = (uint8_t)clamp8((298 * C + 516 * D + 128) >> 8);
            drow[x*4+1] = (uint8_t)clamp8((298 * C - 100 * D - 208 * E + 128) >> 8);
            drow[x*4+2] = (uint8_t)clamp8((298 * C + 409 * E + 128) >> 8);
            drow[x*4+3] = 0xFF;
        }
    }
}

static void yuy2_to_bgra(const uint8_t* src, int pitch,
                         uint8_t* dst, int w, int h) {
    for (int y = 0; y < h; y++) {
        const uint8_t* srow = src + (size_t)y * (size_t)pitch;
        uint8_t* drow = dst + (size_t)(h - 1 - y) * (size_t)w * 4;
        for (int x = 0; x < w; x += 2) {
            int Y0 = srow[x*2+0], U = srow[x*2+1];
            int Y1 = srow[x*2+2], V = srow[x*2+3];
            int C0 = Y0 - 16, D0 = U - 128, E0 = V - 128;
            int C1 = Y1 - 16, D1 = U - 128, E1 = V - 128;
            drow[x*4+0] = (uint8_t)clamp8((298 * C0 + 516 * D0 + 128) >> 8);
            drow[x*4+1] = (uint8_t)clamp8((298 * C0 - 100 * D0 - 208 * E0 + 128) >> 8);
            drow[x*4+2] = (uint8_t)clamp8((298 * C0 + 409 * E0 + 128) >> 8);
            drow[x*4+3] = 0xFF;
            drow[x*4+4] = (uint8_t)clamp8((298 * C1 + 516 * D1 + 128) >> 8);
            drow[x*4+5] = (uint8_t)clamp8((298 * C1 - 100 * D1 - 208 * E1 + 128) >> 8);
            drow[x*4+6] = (uint8_t)clamp8((298 * C1 + 409 * E1 + 128) >> 8);
            drow[x*4+7] = 0xFF;
        }
    }
}

static int try_set_output_type(IMFSourceReader* reader, DWORD stream_idx,
                               const GUID* subtype, UINT32 w, UINT32 h) {
    IMFMediaType* mt = NULL;
    if (FAILED(MFCreateMediaType(&mt))) return 0;
    mt->lpVtbl->SetGUID(mt, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    mt->lpVtbl->SetGUID(mt, &MF_MT_SUBTYPE, subtype);
    mt->lpVtbl->SetUINT32(mt, &MF_MT_INTERLACE_MODE,
                          MFVideoInterlace_Progressive);
    UINT64 sz = ((UINT64)w << 32) | h;
    mt->lpVtbl->SetUINT64(mt, &MF_MT_FRAME_SIZE, sz);
    HRESULT hr = reader->lpVtbl->SetCurrentMediaType(reader, stream_idx, NULL, mt);
    g_last_hresult = (int)hr;
    mt->lpVtbl->Release(mt);
    return SUCCEEDED(hr);
}

static int handle_eof(VideoSlot* v) {
    if (v->loop) {
        PROPVARIANT var;
        PropVariantInit(&var);
        var.vt = VT_I8;
        var.hVal.QuadPart = 0;
        v->reader->lpVtbl->SetCurrentPosition(v->reader, &GUID_NULL, &var);
        PropVariantClear(&var);
        v->reader->lpVtbl->Flush(v->reader, v->video_idx);
        v->cur_time = 0.0;
    }
    return 0;
}

int video_open(const char* path) {
    if (!path || !mf_init()) return 0;

    int slot = -1;
    for (int i = 0; i < MAX_VIDEO; i++)
        if (!slots[i].in_use) { slot = i; break; }
    if (slot < 0) { g_last_error = 7; return 0; }

    wchar_t wpath[MAX_PATH];
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, MAX_PATH) == 0)
        { g_last_error = 8; return 0; }

    IMFSourceReader* reader = NULL;
    if (FAILED(MFCreateSourceReaderFromURL(wpath, NULL, &reader)))
        { g_last_error = 2; return 0; }

    reader->lpVtbl->SetStreamSelection(reader, MF_SOURCE_READER_ALL_STREAMS, FALSE);

    /* 扫描流: 找视频/音频流索引 + 视频宽高 + 音频参数 */
    DWORD video_idx = (DWORD)-1, audio_idx = (DWORD)-1;
    UINT32 w = 0, h = 0;
    UINT32 a_rate = 0, a_ch = 0;
    for (DWORD i = 0; i < 8; i++) {
        IMFMediaType* nt = NULL;
        if (FAILED(reader->lpVtbl->GetNativeMediaType(reader, i, 0, &nt)))
            break;
        GUID maj;
        if (FAILED(nt->lpVtbl->GetGUID(nt, &MF_MT_MAJOR_TYPE, &maj))) {
            nt->lpVtbl->Release(nt);
            continue;
        }
        if (IsEqualGUID(&maj, &MFMediaType_Video) && video_idx == (DWORD)-1) {
            video_idx = i;
            UINT64 size = 0;
            if (SUCCEEDED(nt->lpVtbl->GetUINT64(nt, &MF_MT_FRAME_SIZE, &size))) {
                w = (UINT32)(size >> 32);
                h = (UINT32)(size & 0xFFFFFFFF);
            }
        } else if (IsEqualGUID(&maj, &MFMediaType_Audio) && audio_idx == (DWORD)-1) {
            audio_idx = i;
            nt->lpVtbl->GetUINT32(nt, &MF_MT_AUDIO_SAMPLES_PER_SECOND, &a_rate);
            nt->lpVtbl->GetUINT32(nt, &MF_MT_AUDIO_NUM_CHANNELS, &a_ch);
        }
        nt->lpVtbl->Release(nt);
    }

    if (video_idx == (DWORD)-1) {
        reader->lpVtbl->Release(reader);
        g_last_error = 3;
        return 0;
    }

    if (w == 0 || h == 0) {
        IMFMediaType* cur = NULL;
        if (SUCCEEDED(reader->lpVtbl->GetCurrentMediaType(reader, video_idx, &cur))) {
            UINT64 size = 0;
            if (SUCCEEDED(cur->lpVtbl->GetUINT64(cur, &MF_MT_FRAME_SIZE, &size))) {
                w = (UINT32)(size >> 32);
                h = (UINT32)(size & 0xFFFFFFFF);
            }
            cur->lpVtbl->Release(cur);
        }
    }
    fprintf(stderr, "[video] w=%u h=%u vidx=%u aidx=%u\n",
            w, h, video_idx, audio_idx);
    if (w == 0 || h == 0 || w > 4096 || h > 4096) {
        reader->lpVtbl->Release(reader);
        g_last_error = 3;
        return 0;
    }
    reader->lpVtbl->SetStreamSelection(reader, video_idx, TRUE);

    /* 视频输出格式协商链: RGB32 -> NV12 -> 原生 */
    int is_rgb32 = 0;
    UINT32 final_subtype = 0;
    if (try_set_output_type(reader, video_idx, &MFVideoFormat_RGB32, w, h)) {
        is_rgb32 = 1;
        final_subtype = FOURCC_RGB32;
    } else if (try_set_output_type(reader, video_idx, &MFVideoFormat_NV12, w, h)) {
        final_subtype = FOURCC_NV12;
    } else {
        reader->lpVtbl->SetCurrentMediaType(reader, video_idx, NULL, NULL);
        IMFMediaType* cur = NULL;
        if (FAILED(reader->lpVtbl->GetCurrentMediaType(reader, video_idx, &cur))) {
            reader->lpVtbl->Release(reader);
            g_last_error = 21;
            return 0;
        }
        GUID sub;
        if (SUCCEEDED(cur->lpVtbl->GetGUID(cur, &MF_MT_SUBTYPE, &sub)))
            final_subtype = sub.Data1;
        cur->lpVtbl->Release(cur);
    }
    g_last_subtype = final_subtype;

    if (!is_rgb32 &&
        final_subtype != FOURCC_NV12 && final_subtype != FOURCC_YUY2 &&
        final_subtype != FOURCC_I420 && final_subtype != FOURCC_IYUV &&
        final_subtype != FOURCC_YV12) {
        reader->lpVtbl->Release(reader);
        g_last_error = 20;
        return 0;
    }

    /* ★ 音频: 设 PCM 输出并预解码全部到内存 */
    unsigned char* pcm = NULL;
    int pcm_len = 0, pcm_cap = 0;
    int audio_handle = 0;

    if (audio_idx != (DWORD)-1 && a_rate > 0 && a_ch > 0 && a_ch <= 2) {
        reader->lpVtbl->SetStreamSelection(reader, audio_idx, TRUE);
        IMFMediaType* pcmt = NULL;
        if (SUCCEEDED(MFCreateMediaType(&pcmt))) {
            pcmt->lpVtbl->SetGUID(pcmt, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
            pcmt->lpVtbl->SetGUID(pcmt, &MF_MT_SUBTYPE, &MFAudioFormat_PCM);
            pcmt->lpVtbl->SetUINT32(pcmt, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
            pcmt->lpVtbl->SetUINT32(pcmt, &MF_MT_AUDIO_SAMPLES_PER_SECOND, a_rate);
            pcmt->lpVtbl->SetUINT32(pcmt, &MF_MT_AUDIO_NUM_CHANNELS, a_ch);
            pcmt->lpVtbl->SetUINT32(pcmt, &MF_MT_ALL_SAMPLES_INDEPENDENT, 1);
            reader->lpVtbl->SetCurrentMediaType(reader, audio_idx, NULL, pcmt);
            pcmt->lpVtbl->Release(pcmt);
        }

        for (;;) {
            DWORD ai = 0, af = 0;
            LONGLONG ats = 0;
            IMFSample* as = NULL;
            HRESULT ahr = reader->lpVtbl->ReadSample(
                reader, audio_idx, 0, &ai, &af, &ats, &as);
            if (FAILED(ahr)) break;
            if (af & MF_SOURCE_READERF_ENDOFSTREAM) {
                if (as) as->lpVtbl->Release(as);
                break;
            }
            if (!as) continue;
            IMFMediaBuffer* amb = NULL;
            if (SUCCEEDED(as->lpVtbl->GetBufferByIndex(as, 0, &amb))) {
                BYTE* ad = NULL;
                DWORD amax = 0, acur = 0;
                if (SUCCEEDED(amb->lpVtbl->Lock(amb, &ad, &amax, &acur))) {
                    if (ad && acur > 0) {
                        if (pcm_len + (int)acur > pcm_cap) {
                            int nc = pcm_cap ? pcm_cap * 2 : 65536;
                            while (nc < pcm_len + (int)acur) nc *= 2;
                            unsigned char* np = (unsigned char*)realloc(pcm, (size_t)nc);
                            if (!np) { amb->lpVtbl->Unlock(amb); amb->lpVtbl->Release(amb); as->lpVtbl->Release(as); break; }
                            pcm = np;
                            pcm_cap = nc;
                        }
                        memcpy(pcm + pcm_len, ad, acur);
                        pcm_len += (int)acur;
                    }
                    amb->lpVtbl->Unlock(amb);
                }
                amb->lpVtbl->Release(amb);
            }
            as->lpVtbl->Release(as);
        }

        if (pcm_len > 0)
            audio_handle = audio_load_memory(pcm, pcm_len, (int)a_rate, (int)a_ch);
    }

    /* 时长 */
    double duration = 0.0;
    PROPVARIANT var;
    PropVariantInit(&var);
    if (SUCCEEDED(reader->lpVtbl->GetPresentationAttribute(
            reader, MF_SOURCE_READER_MEDIASOURCE, &MF_PD_DURATION, &var))) {
        duration = (double)var.uhVal.QuadPart / 10000000.0;
    }
    PropVariantClear(&var);

    VideoSlot* v = &slots[slot];
    SIZE_T frame_bytes = (SIZE_T)w * (SIZE_T)h * 4;
    fprintf(stderr, "[video] VirtualAlloc %u*%u*4 = %zu bytes\n",
            w, h, (size_t)frame_bytes);
    v->frame = (unsigned char*)VirtualAlloc(NULL, frame_bytes,
                                            MEM_COMMIT | MEM_RESERVE,
                                            PAGE_READWRITE);
    if (!v->frame) {
        fprintf(stderr, "[video] VirtualAlloc FAILED\n");
        reader->lpVtbl->Release(reader);
        g_last_error = 6;
        return 0;
    }
    fprintf(stderr, "[video] open OK, slot=%d\n", slot + 1);
    v->reader       = reader;
    v->width        = (int)w;
    v->height       = (int)h;
    v->video_idx    = video_idx;
    v->duration     = duration;
    v->cur_time     = 0.0;
    v->subtype      = final_subtype;
    v->is_rgb32     = is_rgb32;
    v->in_use       = 1;
    v->loop         = 0;
    v->got_frame    = 0;
    v->audio_handle = audio_handle;
    v->pcm_data     = pcm;
    v->pcm_len      = pcm_len;
    return slot + 1;
}

void video_close(int handle) {
    int idx = handle - 1;
    if (idx < 0 || idx >= MAX_VIDEO) return;
    VideoSlot* v = &slots[idx];
    if (!v->in_use) return;
    if (v->audio_handle) audio_unload(v->audio_handle);
    if (v->reader) v->reader->lpVtbl->Release(v->reader);
    if (v->frame)  VirtualFree(v->frame, 0, MEM_RELEASE);
    if (v->pcm_data) free(v->pcm_data);
    v->reader = NULL;
    v->frame = NULL;
    v->pcm_data = NULL;
    v->audio_handle = 0;
    v->in_use = 0;
    v->got_frame = 0;
}

int video_decode_frame(int handle) {
    VideoSlot* v = slot_of(handle);
    if (!v) return -1;

    DWORD stream_index = 0, flags = 0;
    LONGLONG timestamp = 0;
    IMFSample* sample = NULL;

    HRESULT hr = v->reader->lpVtbl->ReadSample(
        v->reader, v->video_idx, 0, &stream_index, &flags, &timestamp, &sample);

    if (FAILED(hr))
        return handle_eof(v);

    if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
        if (sample) sample->lpVtbl->Release(sample);
        return handle_eof(v);
    }

    if (!sample)
        return 0;

    int decoded = 0;
    IMFMediaBuffer* mb = NULL;

    if (SUCCEEDED(sample->lpVtbl->GetBufferByIndex(sample, 0, &mb))) {
        IMF2DBuffer* buf2d = NULL;
        if (SUCCEEDED(mb->lpVtbl->QueryInterface(
                mb, &IID_IMF2DBuffer, (void**)&buf2d))) {
            BYTE* data = NULL;
            LONG pitch = 0;
            if (SUCCEEDED(buf2d->lpVtbl->Lock2D(buf2d, &data, &pitch))) {
                if (data && pitch > 0) {
                    if (v->is_rgb32) {
                        /* 源自顶向下 -> v->frame 自底向上 */
                        for (int y = 0; y < v->height; y++) {
                            memcpy(v->frame
                                   + (size_t)(v->height - 1 - y) * (size_t)v->width * 4,
                                   data + (size_t)y * (size_t)pitch,
                                   (size_t)v->width * 4);
                        }
                        decoded = 1;
                    } else if (v->subtype == FOURCC_NV12) {
                        nv12_to_bgra(data, pitch,
                                     data + (size_t)pitch * (size_t)v->height,
                                     pitch,
                                     v->frame, v->width, v->height);
                        decoded = 1;
                    } else if (v->subtype == FOURCC_I420 ||
                               v->subtype == FOURCC_IYUV) {
                        i420_to_bgra(data, pitch,
                                     data + (size_t)pitch * (size_t)v->height,
                                     data + (size_t)pitch * (size_t)v->height
                                          + (size_t)(pitch / 2) * (size_t)((v->height + 1) / 2),
                                     pitch / 2,
                                     v->frame, v->width, v->height);
                        decoded = 1;
                    } else if (v->subtype == FOURCC_YV12) {
                        i420_to_bgra(data, pitch,
                                     data + (size_t)pitch * (size_t)v->height
                                          + (size_t)(pitch / 2) * (size_t)((v->height + 1) / 2),
                                     data + (size_t)pitch * (size_t)v->height,
                                     pitch / 2,
                                     v->frame, v->width, v->height);
                        decoded = 1;
                    } else {
                        yuy2_to_bgra(data, pitch,
                                     v->frame, v->width, v->height);
                        decoded = 1;
                    }
                }
                buf2d->lpVtbl->Unlock2D(buf2d);
            }
            buf2d->lpVtbl->Release(buf2d);
        }
        mb->lpVtbl->Release(mb);
    }
    sample->lpVtbl->Release(sample);

    if (decoded) {
        v->got_frame = 1;
        v->cur_time = (timestamp >= 0) ? (double)timestamp / 10000000.0 : 0.0;
        return 1;
    }
    return 0;
}

int video_get_width(int handle) {
    VideoSlot* v = slot_of(handle);
    return v ? v->width : 0;
}

int video_get_height(int handle) {
    VideoSlot* v = slot_of(handle);
    return v ? v->height : 0;
}

float video_get_fps(int handle) {
    VideoSlot* v = slot_of(handle);
    return v ? 30.0f : 0.0f;
}

float video_get_duration(int handle) {
    VideoSlot* v = slot_of(handle);
    return v ? (float)v->duration : 0.0f;
}

float video_get_time(int handle) {
    VideoSlot* v = slot_of(handle);
    return v ? (float)v->cur_time : 0.0f;
}

/* 开始播放: 视频回到起点, 音频同步从头开始 */
void video_play(int handle, int loop) {
    VideoSlot* v = slot_of(handle);
    if (!v) return;
    v->loop = loop ? 1 : 0;
    video_rewind(handle);
    if (v->audio_handle)
        audio_play(v->audio_handle, v->loop);
}

/* 停止: 音视频都停并回到起点 */
void video_stop(int handle) {
    VideoSlot* v = slot_of(handle);
    if (!v) return;
    if (v->audio_handle)
        audio_stop(v->audio_handle);
    video_rewind(handle);
}

void video_seek(int handle, float sec) {
    VideoSlot* v = slot_of(handle);
    if (!v) return;
    if (sec < 0.0f) sec = 0.0f;
    PROPVARIANT var;
    PropVariantInit(&var);
    var.vt = VT_I8;
    var.hVal.QuadPart = (LONGLONG)(sec * 10000000.0);
    v->reader->lpVtbl->SetCurrentPosition(v->reader, &GUID_NULL, &var);
    PropVariantClear(&var);
    v->reader->lpVtbl->Flush(v->reader, v->video_idx);
    v->cur_time = sec;
}

void video_rewind(int handle) {
    VideoSlot* v = slot_of(handle);
    if (!v) return;
    PROPVARIANT var;
    PropVariantInit(&var);
    var.vt = VT_I8;
    var.hVal.QuadPart = 0;
    v->reader->lpVtbl->SetCurrentPosition(v->reader, &GUID_NULL, &var);
    PropVariantClear(&var);
    v->reader->lpVtbl->Flush(v->reader, v->video_idx);
    v->cur_time = 0.0;
}

void video_set_loop(int handle, int loop) {
    VideoSlot* v = slot_of(handle);
    if (v) v->loop = loop ? 1 : 0;
}

/* ★ v->frame 已是自底向上(DIB 序), 目标 Canvas 也是 DIB 序: 直接整块拷贝, 不再翻转 */
int video_blit_to(int handle, unsigned char* dst, int dw, int dh) {
    VideoSlot* v = slot_of(handle);
    if (!v || !v->got_frame || !dst) return 0;
    if (dw != v->width || dh != v->height) return 0;
    memcpy(dst, v->frame, (size_t)dw * (size_t)dh * 4);
    return 1;
}