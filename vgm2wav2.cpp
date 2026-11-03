/* vgm2wav2 - batch VGM/S98/DRO/GYM/NSF/SPC/... to audio converter
 * Supports: single file, directory (recursive), zip archive
 * Engines: libvgm (VGM/S98/DRO/GYM), libgme (NSF/SPC/GBS/AY/HES/KSS/SAP/NSFE)
 * Based on libvgm's vgm2wav.cpp
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <filesystem>
#ifdef HAVE_LIBZIP
#include <zip.h>
#endif
#ifdef HAVE_GME
#include <gme/gme.h>
#endif

#include "player/playerbase.hpp"
#include "player/vgmplayer.hpp"
#include "player/s98player.hpp"
#include "player/droplayer.hpp"
#include "player/gymplayer.hpp"
#include "player/playera.hpp"
#include "utils/DataLoader.h"
#include "utils/FileLoader.h"
#include "utils/MemoryLoader.h"
#include "emu/SoundDevs.h"
#include "emu/EmuCores.h"

#ifdef _MSC_VER
#define strncasecmp _strnicmp
#define snprintf    _snprintf
#endif
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace fs = std::filesystem;

#define BUFFER_LEN 2048

static double        fade_len      = 8.0;
static unsigned int  sample_rate   = 44100;
static unsigned int  bit_depth     = 16;
static unsigned int  loops         = 2;
static std::string   output_format = "wav";  // wav, mp3, aac, flac, ...
static bool          dry_run       = false;
static bool          skip_existing = false;
static bool          play_mode     = false;
static bool          stdout_mode   = false;

enum class Engine { Auto, LibVGM, GME };
static Engine engine = Engine::Auto;

static const char *extensible_guid_trailer =
    "\x00\x00\x00\x00\x10\x00\x80\x00\x00\xAA\x00\x38\x9B\x71";

/* ---- helpers (same as vgm2wav.cpp) ---- */

static void pack_uint16le(UINT8 *d, UINT16 n) {
    d[0] = (UINT8)((UINT16)n     );
    d[1] = (UINT8)((UINT16)n >> 8);
}

static void pack_uint32le(UINT8 *d, UINT32 n) {
    d[0] = (UINT8)(n      );
    d[1] = (UINT8)(n >>  8);
    d[2] = (UINT8)(n >> 16);
    d[3] = (UINT8)(n >> 24);
}

static inline void repack_int16le(UINT8 *d, const UINT8 *src) {
#ifdef VGM_BIG_ENDIAN
    UINT8 tmp[2]; memcpy(tmp,src,2); d[0]=tmp[1]; d[1]=tmp[0];
#else
    (void)d; (void)src;
#endif
}

static inline void repack_int24le(UINT8 *d, const UINT8 *src) {
#ifdef VGM_BIG_ENDIAN
    UINT8 tmp[3]; memcpy(tmp,src,3); d[0]=tmp[2]; d[1]=tmp[1]; d[2]=tmp[0];
#else
    (void)d; (void)src;
#endif
}

static inline void repack_int32le(UINT8 *d, const UINT8 *src) {
#ifdef VGM_BIG_ENDIAN
    UINT8 tmp[4]; memcpy(tmp,src,4); d[0]=tmp[3]; d[1]=tmp[2]; d[2]=tmp[1]; d[3]=tmp[0];
#else
    (void)d; (void)src;
#endif
}

static int write_wav_header(FILE *f, unsigned int totalFrames, unsigned int bps) {
    unsigned int dataSize = totalFrames * (bps / 8) * 2;
    UINT8 tmp[4];
    if(fwrite("RIFF",1,4,f) != 4) return 0;
    pack_uint32le(tmp, 4 + (8 + dataSize) + (8 + 40));
    if(fwrite(tmp,1,4,f) != 4) return 0;
    if(fwrite("WAVE",1,4,f) != 4) return 0;
    if(fwrite("fmt ",1,4,f) != 4) return 0;
    pack_uint32le(tmp,40);
    if(fwrite(tmp,1,4,f) != 4) return 0;
    pack_uint16le(tmp,0xFFFE);
    if(fwrite(tmp,1,2,f) != 2) return 0;
    pack_uint16le(tmp,2);
    if(fwrite(tmp,1,2,f) != 2) return 0;
    pack_uint32le(tmp,sample_rate);
    if(fwrite(tmp,1,4,f) != 4) return 0;
    pack_uint32le(tmp,sample_rate * 2 * (bps / 8));
    if(fwrite(tmp,1,4,f) != 4) return 0;
    pack_uint16le(tmp,2 * (bps / 8));
    if(fwrite(tmp,1,2,f) != 2) return 0;
    pack_uint16le(tmp,bps);
    if(fwrite(tmp,1,2,f) != 2) return 0;
    pack_uint16le(tmp,22);
    if(fwrite(tmp,1,2,f) != 2) return 0;
    pack_uint16le(tmp,bps);
    if(fwrite(tmp,1,2,f) != 2) return 0;
    pack_uint32le(tmp,3);
    if(fwrite(tmp,1,4,f) != 4) return 0;
    pack_uint16le(tmp,1);
    if(fwrite(tmp,1,2,f) != 2) return 0;
    if(fwrite(extensible_guid_trailer,1,14,f) != 14) return 0;
    if(fwrite("data",1,4,f) != 4) return 0;
    pack_uint32le(tmp,dataSize);
    if(fwrite(tmp,1,4,f) != 4) return 0;
    return 1;
}

static void frames_to_little_endian(UINT8 *data, unsigned int frame_count) {
    unsigned int i = 0;
    while(i < frame_count) {
        switch(bit_depth) {
            case 32: repack_int32le(&data[0],&data[0]); repack_int32le(&data[4],&data[4]); break;
            case 24: repack_int24le(&data[0],&data[0]); repack_int24le(&data[3],&data[3]); break;
            default: repack_int16le(&data[0],&data[0]); repack_int16le(&data[2],&data[2]); break;
        }
        i++;
        data += (bit_depth / 8) * 2;
    }
}

/* ---- ffmpeg helpers ---- */

static std::string format_extension() {
    if(output_format == "wav")  return ".wav";
    if(output_format == "aac")  return ".m4a";
    return "." + output_format;
}

static std::string shell_quote(const std::string &s) {
#ifdef _WIN32
    // cmd.exe uses double-quote quoting; escape embedded double quotes
    std::string r = "\"";
    for(char c : s) {
        if(c == '"') r += "\\\"";
        else r += c;
    }
    return r + "\"";
#else
    std::string r = "'";
    for(char c : s) {
        if(c == '\'') r += "'\\''";
        else r += c;
    }
    return r + "'";
#endif
}

static const char *ffmpeg_pcm_fmt() {
    if(bit_depth == 24) return "s24le";
    if(bit_depth == 32) return "s32le";
    return "s16le";
}

static bool check_ffmpeg() {
#ifdef _WIN32
    FILE *p = popen("ffmpeg -version >NUL 2>&1", "r");
#else
    FILE *p = popen("ffmpeg -version > /dev/null 2>&1", "r");
#endif
    if(!p) return false;
    return pclose(p) == 0;
}

static bool check_ffplay() {
#ifdef _WIN32
    FILE *p = popen("ffplay -version >NUL 2>&1", "r");
#else
    FILE *p = popen("ffplay -version > /dev/null 2>&1", "r");
#endif
    if(!p) return false;
    return pclose(p) == 0;
}

// Returns true if output goes through a pipe (play or encode)
static bool is_piped_output() {
    return play_mode || stdout_mode || output_format != "wav";
}

static FILE *open_pipe_write(const std::string &cmd) {
    // Use "wb" (binary mode) to prevent Windows CRT from translating
    // 0x0A bytes to 0x0D 0x0A, which corrupts raw PCM data.
    // POSIX popen() only accepts "r"/"w" (macOS rejects "wb" with EINVAL).
#ifdef _WIN32
    FILE *f = popen(cmd.c_str(), "wb");
#else
    FILE *f = popen(cmd.c_str(), "w");
#endif
    // Increase userspace write buffer to reduce pipe-blocking stutter on
    // Windows (default pipe buffer is 4 KB; ffplay reads in ~8 KB chunks).
    if(f) setvbuf(f, NULL, _IOFBF, 256 * 1024);
    return f;
}

static FILE *open_output(const char *out_path) {
    if(play_mode) {
        std::string cmd = std::string("ffplay -nodisp -autoexit")
            + " -f "  + ffmpeg_pcm_fmt()
            + " -ar " + std::to_string(sample_rate)
            + " -ch_layout stereo -i -";
        return open_pipe_write(cmd);
    }
    if(stdout_mode) {
#ifdef _WIN32
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        return stdout;
    }
    if(output_format != "wav") {
#ifdef _WIN32
        const char *null_dev = "2>NUL";
#else
        const char *null_dev = "2>/dev/null";
#endif
        std::string cmd = std::string("ffmpeg -y")
            + " -f "  + ffmpeg_pcm_fmt()
            + " -ar " + std::to_string(sample_rate)
            + " -ac 2"
            + " -i pipe:0"
            + " " + shell_quote(out_path)
            + " " + null_dev;
        return open_pipe_write(cmd);
    }
    return fopen(out_path, "wb");
}

static FILE *open_output_s16le(const char *out_path) {
    if(play_mode) {
        std::string cmd = std::string("ffplay -nodisp -autoexit")
            + " -f s16le"
            + " -ar " + std::to_string(sample_rate)
            + " -ch_layout stereo -i -";
        return open_pipe_write(cmd);
    }
    if(stdout_mode) {
#ifdef _WIN32
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        return stdout;
    }
    if(output_format != "wav") {
#ifdef _WIN32
        const char *null_dev = "2>NUL";
#else
        const char *null_dev = "2>/dev/null";
#endif
        std::string cmd = std::string("ffmpeg -y")
            + " -f s16le"
            + " -ar " + std::to_string(sample_rate)
            + " -ac 2"
            + " -i pipe:0"
            + " " + shell_quote(out_path)
            + " " + null_dev;
        return open_pipe_write(cmd);
    }
    return fopen(out_path, "wb");
}

static void close_output(FILE *f) {
    if(stdout_mode) { fflush(f); return; }
    if(play_mode || output_format != "wav") pclose(f);
    else fclose(f);
}

/* ---- input type helpers ---- */

static bool is_vgm_ext(const fs::path &p) {
    std::string ext = p.extension().string();
    for(auto &c : ext) c = (char)tolower((unsigned char)c);
    return ext == ".vgm" || ext == ".vgz" || ext == ".s98"
        || ext == ".dro" || ext == ".gym";
}

static bool is_gme_only_ext(const fs::path &p) {
    std::string ext = p.extension().string();
    for(auto &c : ext) c = (char)tolower((unsigned char)c);
    return ext == ".ay"  || ext == ".gbs"  || ext == ".hes" || ext == ".kss"
        || ext == ".nsf" || ext == ".nsfe" || ext == ".sap" || ext == ".spc";
}

static bool is_music_ext(const fs::path &p) {
    return is_vgm_ext(p) || is_gme_only_ext(p);
}

static bool should_use_gme(const std::string &path) {
    switch(engine) {
        case Engine::LibVGM: return false;
        case Engine::GME:    return true;
        case Engine::Auto:   return is_gme_only_ext(fs::path(path));
    }
    return false;
}

// For multi-track output: if total==1 return out_path unchanged,
// otherwise insert _NN before the extension (1-indexed).
static std::string gme_track_path(const std::string &out_path, int track, int total) {
    if(total == 1) return out_path;
    fs::path p(out_path);
    char suffix[8];
    snprintf(suffix, sizeof(suffix), "_%02d", track + 1);
    std::string name = p.stem().string() + suffix + p.extension().string();
    return (p.parent_path() / name).string();
}

/* ---- libvgm core render ---- */

static int render_to_file(DATA_LOADER *loader, const char *in_name, const char *out_path) {
    PlayerA player;

    UINT8 *packed = (UINT8 *)malloc(sizeof(INT32) * 2 * BUFFER_LEN);
    if(!packed) { fprintf(stderr, "out of memory\n"); return 1; }

    player.RegisterPlayerEngine(new VGMPlayer);
    player.RegisterPlayerEngine(new S98Player);
    player.RegisterPlayerEngine(new DROPlayer);
    player.RegisterPlayerEngine(new GYMPlayer);

    if(player.SetOutputSettings(sample_rate, 2, bit_depth, BUFFER_LEN)) {
        fprintf(stderr, "Unsupported sample rate / bps\n");
        free(packed);
        return 1;
    }

    {
        PlayerA::Config pCfg = player.GetConfiguration();
        pCfg.masterVol = 0x10000;
        pCfg.loopCount = loops;
        pCfg.fadeSmpls = (UINT32)(sample_rate * fade_len);
        pCfg.endSilenceSmpls = 0;
        pCfg.pbSpeed = 1.0;
        player.SetConfiguration(pCfg);
    }

    DataLoader_SetPreloadBytes(loader, 0x100);
    if(DataLoader_Load(loader)) {
        fprintf(stderr, "Failed to load: %s\n", in_name);
        free(packed);
        return 1;
    }

    if(player.LoadFile(loader)) {
        fprintf(stderr, "Failed to parse: %s\n", in_name);
        free(packed);
        return 1;
    }

    PlayerBase *plrEngine = player.GetPlayer();
    if(plrEngine->GetPlayerType() == FCC_VGM) {
        VGMPlayer *vgmplay = dynamic_cast<VGMPlayer *>(plrEngine);
        player.SetLoopCount(vgmplay->GetModifiedLoopCount(loops));
    }

    FILE *f = open_output(out_path);
    if(!f) {
        fprintf(stderr, "Cannot open output: %s\n", play_mode ? "(playback)" : out_path);
        free(packed);
        return 1;
    }

    player.Start();

    unsigned int totalFrames = plrEngine->Tick2Sample(plrEngine->GetTotalPlayTicks(loops));
    if(plrEngine->GetLoopTicks() > 0)
        totalFrames += player.GetFadeSamples();

    if(play_mode)
        fprintf(stderr, "Playing: %s  [%.1fs]\n",
            in_name, plrEngine->Sample2Second(totalFrames));
    else if(stdout_mode)
        fprintf(stderr, "%s  [%.1fs]\n",
            in_name, plrEngine->Sample2Second(totalFrames));
    else
        fprintf(stderr, "%s -> %s  [%.1fs]\n",
            in_name, out_path, plrEngine->Sample2Second(totalFrames));

    if(!is_piped_output())
        write_wav_header(f, totalFrames, bit_depth);

    double complete = 0.0;
    double inc = (totalFrames > 0) ? (double)BUFFER_LEN / totalFrames : 1.0;
    if(!stdout_mode) { fprintf(stderr, "["); fflush(stderr); }

    while(totalFrames) {
        memset(packed, 0, sizeof(INT32) * BUFFER_LEN * 2);
        unsigned int curFrames = (BUFFER_LEN > totalFrames ? totalFrames : BUFFER_LEN);
        player.Render(curFrames * ((bit_depth / 8) * 2), packed);
        frames_to_little_endian(packed, curFrames);
        fwrite(packed, (bit_depth / 8) * 2, curFrames, f);
        totalFrames -= curFrames;
        complete += inc;
        if(!stdout_mode && complete >= 0.10) { complete -= 0.10; fprintf(stderr, "-"); fflush(stderr); }
    }
    if(!stdout_mode) fprintf(stderr, "]\n");

    player.Stop();
    player.UnloadFile();
    player.UnregisterAllPlayers();
    free(packed);
    close_output(f);
    return 0;
}

/* ---- GME render functions ---- */

#ifdef HAVE_GME

static int render_gme_track(Music_Emu *emu, int track_idx,
                             const char *in_name, const char *out_path) {
    if(skip_existing && fs::exists(out_path)) {
        fprintf(stderr, "Skip: %s (exists)\n", out_path);
        return 0;
    }
    if(dry_run) {
        fprintf(stderr, "Would convert: %s [track %d] -> %s\n",
            in_name, track_idx + 1, out_path);
        return 0;
    }
    gme_info_t *info = NULL;
    gme_track_info(emu, &info, track_idx);

    long play_ms;
    if(info && info->length > 0)
        play_ms = info->length;
    else if(info && info->loop_length > 0)
        play_ms = info->intro_length + info->loop_length * (long)loops;
    else
        play_ms = 150000;  // 2.5 min default

    long total_ms = play_ms + (long)(fade_len * 1000);
    if(info) gme_free_info(info);

    gme_set_fade(emu, play_ms);

    gme_err_t err = gme_start_track(emu, track_idx);
    if(err) {
        fprintf(stderr, "GME start_track error (%s track %d): %s\n",
            in_name, track_idx + 1, err);
        return 1;
    }

    unsigned int total_frames = (unsigned int)((long long)total_ms * sample_rate / 1000);

    FILE *f = open_output_s16le(out_path);
    if(!f) {
        fprintf(stderr, "Cannot open output: %s\n", play_mode ? "(playback)" : out_path);
        return 1;
    }

    if(play_mode)
        fprintf(stderr, "Playing: %s [track %d]  [%.1fs]\n",
            in_name, track_idx + 1, total_ms / 1000.0);
    else if(stdout_mode)
        fprintf(stderr, "%s [track %d]  [%.1fs]\n",
            in_name, track_idx + 1, total_ms / 1000.0);
    else
        fprintf(stderr, "%s [track %d] -> %s  [%.1fs]\n",
            in_name, track_idx + 1, out_path, total_ms / 1000.0);

    if(!is_piped_output())
        write_wav_header(f, total_frames, 16);

    std::vector<short> buf(BUFFER_LEN * 2);
    unsigned int frames_left = total_frames;
    double complete = 0.0;
    double inc = (frames_left > 0) ? (double)BUFFER_LEN / frames_left : 1.0;
    if(!stdout_mode) { fprintf(stderr, "["); fflush(stderr); }

    while(frames_left > 0 && !gme_track_ended(emu)) {
        unsigned int cur = (BUFFER_LEN < frames_left) ? BUFFER_LEN : frames_left;
        gme_err_t play_err = gme_play(emu, (int)(cur * 2), buf.data());
        if(play_err) break;
        fwrite(buf.data(), sizeof(short), cur * 2, f);
        frames_left -= cur;
        complete += inc;
        if(!stdout_mode && complete >= 0.10) { complete -= 0.10; fprintf(stderr, "-"); fflush(stderr); }
    }
    if(!stdout_mode) fprintf(stderr, "]\n");

    close_output(f);
    return 0;
}

static int process_gme_file(const std::string &in_path, const std::string &out_path) {
    Music_Emu *emu = NULL;
    gme_err_t err = gme_open_file(in_path.c_str(), &emu, (int)sample_rate);
    if(err) {
        fprintf(stderr, "GME error (%s): %s\n", in_path.c_str(), err);
        return 1;
    }

    int track_count = gme_track_count(emu);
    int errors = 0;
    for(int t = 0; t < track_count; t++) {
        std::string tpath = gme_track_path(out_path, t, track_count);
        // ensure parent directory exists
        { auto p = fs::path(tpath).parent_path(); if(!p.empty()) fs::create_directories(p); }
        errors += render_gme_track(emu, t, in_path.c_str(), tpath.c_str());
    }

    gme_delete(emu);
    return errors;
}

static int process_gme_data(const void *data, long size,
                             const char *in_name, const std::string &out_path) {
    Music_Emu *emu = NULL;
    gme_err_t err = gme_open_data(data, size, &emu, (int)sample_rate);
    if(err) {
        fprintf(stderr, "GME error (%s): %s\n", in_name, err);
        return 1;
    }

    int track_count = gme_track_count(emu);
    int errors = 0;
    for(int t = 0; t < track_count; t++) {
        std::string tpath = gme_track_path(out_path, t, track_count);
        { auto p = fs::path(tpath).parent_path(); if(!p.empty()) fs::create_directories(p); }
        errors += render_gme_track(emu, t, in_name, tpath.c_str());
    }

    gme_delete(emu);
    return errors;
}

#endif  // HAVE_GME

/* ---- file/directory/zip processors ---- */

static int process_file(const std::string &in_path, const std::string &out_path) {
#ifdef HAVE_GME
    if(should_use_gme(in_path))
        return process_gme_file(in_path, out_path);
#endif
    if(skip_existing && fs::exists(out_path)) {
        fprintf(stderr, "Skip: %s (exists)\n", out_path.c_str());
        return 0;
    }
    if(dry_run) {
        fprintf(stderr, "Would convert: %s -> %s\n", in_path.c_str(), out_path.c_str());
        return 0;
    }
    DATA_LOADER *loader = FileLoader_Init(in_path.c_str());
    if(!loader) { fprintf(stderr, "Failed to open: %s\n", in_path.c_str()); return 1; }
    int ret = render_to_file(loader, in_path.c_str(), out_path.c_str());
    DataLoader_Deinit(loader);
    return ret;
}

static int process_directory(const std::string &in_dir, const std::string &out_dir) {
    if(!play_mode) fs::create_directories(out_dir);
    int errors = 0;
    for(auto &entry : fs::recursive_directory_iterator(in_dir)) {
        if(!entry.is_regular_file() || !is_music_ext(entry.path())) continue;
        std::string out_path;
        if(!play_mode) {
            fs::path rel = fs::relative(entry.path(), in_dir);
            fs::path out = fs::path(out_dir) / rel;
            out.replace_extension(format_extension());
            fs::create_directories(out.parent_path());
            out_path = out.string();
        }
        errors += process_file(entry.path().string(), out_path);
    }
    return errors;
}

static int process_zip(const std::string &zip_path, const std::string &out_dir) {
#ifdef HAVE_LIBZIP
    int err = 0;
    zip_t *za = zip_open(zip_path.c_str(), ZIP_RDONLY, &err);
    if(!za) {
        fprintf(stderr, "Failed to open zip: %s (err=%d)\n", zip_path.c_str(), err);
        return 1;
    }
    fs::create_directories(out_dir);
    int errors = 0;
    zip_int64_t n = zip_get_num_entries(za, 0);

    for(zip_int64_t i = 0; i < n; i++) {
        zip_stat_t st;
        if(zip_stat_index(za, i, 0, &st) != 0) continue;
        if(!(st.valid & ZIP_STAT_NAME) || !(st.valid & ZIP_STAT_SIZE)) continue;

        fs::path entry_path(st.name);
        if(!is_music_ext(entry_path)) continue;

        zip_file_t *zf = zip_fopen_index(za, i, 0);
        if(!zf) { errors++; continue; }

        std::vector<UINT8> buf((size_t)st.size);
        if(zip_fread(zf, buf.data(), st.size) != (zip_int64_t)st.size) {
            fprintf(stderr, "Read error in zip entry: %s\n", st.name);
            zip_fclose(zf);
            errors++;
            continue;
        }
        zip_fclose(zf);

        fs::path out = fs::path(out_dir) / entry_path;
        out.replace_extension(format_extension());
        fs::create_directories(out.parent_path());

#ifdef HAVE_GME
        if(should_use_gme(st.name)) {
            errors += process_gme_data(buf.data(), (long)buf.size(),
                                       st.name, out.string());
            continue;
        }
#endif
        if(skip_existing && fs::exists(out)) {
            fprintf(stderr, "Skip: %s (exists)\n", out.string().c_str());
            continue;
        }
        if(dry_run) {
            fprintf(stderr, "Would convert: %s -> %s\n", st.name, out.string().c_str());
            continue;
        }
        DATA_LOADER *loader = MemoryLoader_Init(buf.data(), (UINT32)buf.size());
        if(!loader) { errors++; continue; }

        errors += render_to_file(loader, st.name, out.string().c_str());
        DataLoader_Deinit(loader);
    }
    zip_close(za);
    return errors;
#else
    (void)zip_path; (void)out_dir;
    fprintf(stderr, "ZIP support not available (build with libzip)\n");
    return 1;
#endif
}

/* ---- main ---- */

static unsigned int scan_uint(const char *str) {
    unsigned int num = 0;
    while(*str >= '0' && *str <= '9') { num = num * 10 + (*str - '0'); str++; }
    return num;
}

#define str_equals(s1,s2)    (strcmp(s1,s2) == 0)
#define str_istarts(s1,s2)   (strncasecmp(s1,s2,strlen(s2)) == 0)

int main(int argc, const char *argv[]) {
    const char *self = *argv++;
    argc--;

    while(argc > 0) {
        const char *arg = *argv;
        if(str_equals(arg, "--")) { argv++; argc--; break; }

        const char *val = NULL;
        const char *eq  = strchr(arg, '=');

        auto next_val = [&]() -> const char * {
            if(eq) return eq + 1;
            if(argc > 1) { argv++; argc--; return *argv; }
            return NULL;
        };

        if(str_istarts(arg, "--loops"))      { val = next_val(); if(val) loops         = scan_uint(val); }
        else if(str_istarts(arg, "--samplerate")) { val = next_val(); if(val) sample_rate = scan_uint(val); }
        else if(str_istarts(arg, "--bps"))   { val = next_val(); if(val) bit_depth    = scan_uint(val); }
        else if(str_istarts(arg, "--fade"))  { val = next_val(); if(val) fade_len     = strtod(val, NULL); }
        else if(str_istarts(arg, "--format")) { val = next_val(); if(val) output_format = val; }
        else if(str_istarts(arg, "--engine")) {
            val = next_val();
            if(val) {
                if(str_equals(val, "libvgm"))  engine = Engine::LibVGM;
                else if(str_equals(val, "gme")) engine = Engine::GME;
                else                            engine = Engine::Auto;
            }
        }
        else if(str_equals(arg, "--dryrun"))  { dry_run = true; }
        else if(str_equals(arg, "--skip"))    { skip_existing = true; }
        else if(str_equals(arg, "--play"))    { play_mode = true; }
        else if(str_equals(arg, "--stdout"))  { stdout_mode = true; }
        else break;

        argv++; argc--;
    }

    if(loops == 0)       loops       = 2;
    if(sample_rate == 0) sample_rate = 44100;
    if(bit_depth != 16 && bit_depth != 24 && bit_depth != 32) bit_depth = 16;

    if(argc < 1) {
        fprintf(stderr,
            "Usage: %s [options] <input> [output]\n"
            "  input   : audio file, directory, or .zip archive\n"
            "  output  : audio file (single input) or directory (folder/zip input)\n"
            "            (omit to use current directory with input filename)\n"
            "\n"
            "Supported formats:\n"
            "  libvgm  : VGM, VGZ, S98, DRO, GYM\n"
#ifdef HAVE_GME
            "  GME     : AY, GBS, HES, KSS, NSF, NSFE, SAP, SPC\n"
#endif
            "\n"
            "Options:\n"
            "  --play          play directly in terminal (requires ffplay)\n"
            "  --stdout        output raw PCM (s16le 44100 stereo) to stdout\n"
            "  --format fmt    output format: wav, mp3, aac, flac, ... (default: wav)\n"
            "  --engine e      engine: auto, libvgm, gme (default: auto)\n"
            "  --samplerate n  (default: 44100)\n"
            "  --bps n         16/24/32 (default: 16; ignored for GME engine)\n"
            "  --fade x        fade-out seconds (default: 8.0)\n"
            "  --loops n       loop count (default: 2)\n"
            "  --skip          skip if output file already exists (default: overwrite)\n"
            "  --dryrun        print what would be done without converting\n",
            self);
        return 1;
    }

    if(play_mode && stdout_mode) {
        fprintf(stderr, "error: --play and --stdout are mutually exclusive\n");
        return 1;
    }
    if(play_mode && !check_ffplay()) {
        fprintf(stderr, "error: ffplay not found. Install ffmpeg (includes ffplay) to use --play\n");
        return 1;
    }
    if(!play_mode && output_format != "wav" && !check_ffmpeg()) {
        fprintf(stderr, "error: ffmpeg not found. Install ffmpeg to use --format %s\n",
            output_format.c_str());
        return 1;
    }

    fs::path in(argv[0]);
    std::string out = (argc >= 2) ? argv[1] : "";

    if(fs::is_directory(in)) {
        if(out.empty() && !play_mode && !stdout_mode)
            out = fs::current_path().string();
        return process_directory(in.string(), out);
    } else {
        std::string ext = in.extension().string();
        for(auto &c : ext) c = (char)tolower((unsigned char)c);
        if(ext == ".zip") {
            if(out.empty() && !play_mode && !stdout_mode)
                out = (fs::current_path() / in.stem()).string();
            return process_zip(in.string(), out);
        } else {
            if(out.empty() && !play_mode && !stdout_mode)
                out = (fs::current_path() / in.stem()).string() + format_extension();
            return process_file(in.string(), out);
        }
    }
}
