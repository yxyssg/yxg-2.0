/* ============================================================
 * music_loader.c — YXG 音频运行时 (miniaudio 封装)
 *
 * 编译: gcc -m32 -O2 -c music_loader.c -o music_loader.obj
 * 链接: ... music_loader.obj -lole32 -lwinmm -luuid
 *
 * 支持格式(内置解码器): WAV / MP3 / FLAC
 * 新增: audio_load_memory — 从内存 PCM(S16LE 交织)创建音源,
 *       供 video_loader.c 播放视频音轨使用
 *
 * 导出(cdecl):
 *   int  audio_load(const char* path);
 *   int  audio_load_memory(const unsigned char* data, int bytes,
 *                          int sample_rate, int channels);
 *   void audio_play(int handle, int loop);
 *   void audio_pause(int handle);
 *   void audio_resume(int handle);
 *   void audio_stop(int handle);
 *   int  audio_is_playing(int handle);
 *   void audio_set_volume(int handle, int vol);
 *   int  audio_get_position(int handle);
 *   void audio_unload(int handle);
 * ============================================================ */

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <stdlib.h>

#define MAX_MUSIC 64

typedef struct {
    ma_sound*        sound;
    ma_audio_buffer* abuf;      /* 内存音源时才非 NULL */
    ma_uint64        pause_cursor;
    int              in_use;
} MusicSlot;

static ma_engine  engine;
static int        engine_ok = 0;
static MusicSlot  slots[MAX_MUSIC];

static int ensure_engine(void) {
    if (engine_ok) return 1;
    if (ma_engine_init(NULL, &engine) != MA_SUCCESS) return 0;
    engine_ok = 1;
    return 1;
}

static MusicSlot* slot_of(int handle) {
    int idx = handle - 1;
    if (idx < 0 || idx >= MAX_MUSIC) return NULL;
    if (!slots[idx].in_use || !slots[idx].sound) return NULL;
    return &slots[idx];
}

int audio_load(const char* path) {
    if (!path || !ensure_engine()) return 0;
    for (int i = 0; i < MAX_MUSIC; i++) {
        if (slots[i].in_use) continue;
        ma_sound* snd = (ma_sound*)malloc(sizeof(ma_sound));
        if (!snd) return 0;
        ma_result r = ma_sound_init_from_file(&engine, path, 0, NULL, NULL, snd);
        if (r != MA_SUCCESS) {
            free(snd);
            return 0;
        }
        slots[i].sound        = snd;
        slots[i].abuf         = NULL;
        slots[i].pause_cursor = 0;
        slots[i].in_use       = 1;
        return i + 1;
    }
    return 0;
}

int audio_load_memory(const unsigned char* data, int bytes,
                      int sample_rate, int channels) {
    if (!data || bytes <= 0 || !ensure_engine()) return 0;
    if (channels < 1) channels = 1;
    if (channels > 2) channels = 2;
    if (sample_rate < 8000) sample_rate = 44100;

    size_t frames = (size_t)bytes / (size_t)(channels * 2);

    for (int i = 0; i < MAX_MUSIC; i++) {
        if (slots[i].in_use) continue;
        ma_audio_buffer* ab  = (ma_audio_buffer*)malloc(sizeof(ma_audio_buffer));
        ma_sound*        snd = (ma_sound*)malloc(sizeof(ma_sound));
        if (!ab || !snd) { free(ab); free(snd); return 0; }

        ma_audio_buffer_config cfg = ma_audio_buffer_config_init(
            ma_format_s16, (ma_uint32)channels, (ma_uint64)frames,
            data, NULL);
        if (ma_audio_buffer_init(&cfg, ab) != MA_SUCCESS) {
            free(ab); free(snd); return 0;
        }
        if (ma_sound_init_from_data_source(&engine, ab, 0, NULL, snd) != MA_SUCCESS) {
            ma_audio_buffer_uninit(ab);
            free(ab); free(snd); return 0;
        }
        slots[i].sound        = snd;
        slots[i].abuf         = ab;
        slots[i].pause_cursor = 0;
        slots[i].in_use       = 1;
        return i + 1;
    }
    return 0;
}

void audio_play(int handle, int loop) {
    MusicSlot* s = slot_of(handle);
    if (!s) return;
    ma_sound_set_looping(s->sound, loop ? MA_TRUE : MA_FALSE);
    ma_sound_seek_to_pcm_frame(s->sound, 0);
    ma_sound_start(s->sound);
}

void audio_pause(int handle) {
    MusicSlot* s = slot_of(handle);
    if (!s) return;
    if (ma_sound_is_playing(s->sound)) {
        ma_uint64 cur = 0;
        ma_sound_get_cursor_in_pcm_frames(s->sound, &cur);
        s->pause_cursor = cur;
        ma_sound_stop(s->sound);
    }
}

void audio_resume(int handle) {
    MusicSlot* s = slot_of(handle);
    if (!s) return;
    if (!ma_sound_is_playing(s->sound)) {
        ma_sound_seek_to_pcm_frame(s->sound, s->pause_cursor);
        ma_sound_start(s->sound);
    }
}

void audio_stop(int handle) {
    MusicSlot* s = slot_of(handle);
    if (!s) return;
    ma_sound_stop(s->sound);
    ma_sound_seek_to_pcm_frame(s->sound, 0);
    s->pause_cursor = 0;
}

int audio_is_playing(int handle) {
    MusicSlot* s = slot_of(handle);
    return (s && ma_sound_is_playing(s->sound)) ? 1 : 0;
}

void audio_set_volume(int handle, int vol) {
    MusicSlot* s = slot_of(handle);
    if (!s) return;
    float v = (float)vol / 100.0f;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    ma_sound_set_volume(s->sound, v);
}

int audio_get_position(int handle) {
    MusicSlot* s = slot_of(handle);
    if (!s) return 0;
    float sec = 0.0f;
    ma_sound_get_cursor_in_seconds(s->sound, &sec);
    return (int)(sec * 1000.0f);
}
void audio_unload(int handle) {
    int idx = handle - 1;
    if (idx < 0 || idx >= MAX_MUSIC) return;
    if (!slots[idx].in_use) return;
    if (slots[idx].sound) {
        ma_sound_uninit(slots[idx].sound);
        free(slots[idx].sound);
    }
    if (slots[idx].abuf) {
        ma_audio_buffer_uninit(slots[idx].abuf);
        free(slots[idx].abuf);
    }
    slots[idx].sound        = NULL;
    slots[idx].abuf         = NULL;
    slots[idx].pause_cursor = 0;
    slots[idx].in_use       = 0;
}