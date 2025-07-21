#include <jni.h>
#include <android/native_window_jni.h>
#include <unistd.h>
#include "PrecompiledHeader.h"
#include "common/StringUtil.h"
#include "common/FileSystem.h"
#include "common/ZipHelpers.h"
#include "pcsx2/GS.h"
#include "pcsx2/VMManager.h"
#include "PerformanceMetrics.h"
#include "GameList.h"
#include "GS/GSPerfMon.h"
#include "GSDumpReplayer.h"
#include "ImGui/ImGuiManager.h"
#include "common/Path.h"
#include "common/MemorySettingsInterface.h"
#include "SIO/Pad/Pad.h"
#include "Input/InputManager.h"
#include "ImGui/ImGuiFullscreen.h"
#include "Achievements.h"
#include "Host.h"
#include "ImGui/FullscreenUI.h"
#include "SIO/Pad/PadDualshock2.h"
#include "MTGS.h"
#include "SDL3/SDL.h"
#include <future>


bool s_execute_exit;
int s_window_width = 0;
int s_window_height = 0;
ANativeWindow* s_window = nullptr;

static MemorySettingsInterface s_settings_interface;

////
std::string GetJavaString(JNIEnv *env, jstring jstr) {
    if (!jstr) {
        return "";
    }
    const char *str = env->GetStringUTFChars(jstr, nullptr);
    std::string cpp_string = std::string(str);
    env->ReleaseStringUTFChars(jstr, str);
    return cpp_string;
}

extern "C"
JNIEXPORT void JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_initialize(JNIEnv *env, jclass clazz,
                                                jstring p_szpath, jint p_apiVer) {
    std::string _szPath = GetJavaString(env, p_szpath);
    EmuFolders::AppRoot = _szPath;
    EmuFolders::DataRoot = _szPath;
    EmuFolders::SetResourcesDirectory();

    Log::SetConsoleOutputLevel(LOGLEVEL_DEBUG);
    ImGuiManager::SetFontPathAndRange(Path::Combine(EmuFolders::Resources, "fonts" FS_OSPATH_SEPARATOR_STR "Roboto-Regular.ttf"), {});

    bool _SettingsIsEmpty = s_settings_interface.IsEmpty();
    if(_SettingsIsEmpty) {
        // don't provide an ini path, or bother loading. we'll store everything in memory.
        MemorySettingsInterface &si = s_settings_interface;
        Host::Internal::SetBaseSettingsLayer(&si);

        VMManager::SetDefaultSettings(si, true, true, true, true, true);

        // complete as quickly as possible
        si.SetBoolValue("EmuCore/GS", "FrameLimitEnable", false);
        si.SetIntValue("EmuCore/GS", "VsyncEnable", false);

        // ensure all input sources are disabled, we're not using them
        si.SetBoolValue("InputSources", "SDL", true);
        si.SetBoolValue("InputSources", "XInput", false);

        // we don't need any sound output
        si.SetStringValue("SPU2/Output", "OutputModule", "nullout");

        // none of the bindings are going to resolve to anything
        Pad::ClearPortBindings(si, 0);
        si.ClearSection("Hotkeys");

        // force logging
        //si.SetBoolValue("Logging", "EnableSystemConsole", !s_no_console);
        si.SetBoolValue("Logging", "EnableSystemConsole", true);
        si.SetBoolValue("Logging", "EnableTimestamps", true);
        si.SetBoolValue("Logging", "EnableVerbose", true);

        // and show some stats :)
        si.SetBoolValue("EmuCore/GS", "OsdShowFPS", true);
        si.SetBoolValue("EmuCore/GS", "OsdShowResolution", false);
        si.SetBoolValue("EmuCore/GS", "OsdShowGSStats", false);

//        // remove memory cards, so we don't have sharing violations
//        for (u32 i = 0; i < 2; i++)
//        {
//            si.SetBoolValue("MemoryCards", fmt::format("Slot{}_Enable", i + 1).c_str(), false);
//            si.SetStringValue("MemoryCards", fmt::format("Slot{}_Filename", i + 1).c_str(), "");
//        }
    }

    VMManager::Internal::LoadStartupSettings();
}

extern "C"
JNIEXPORT jstring JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_getGameTitle(JNIEnv *env, jclass clazz,
                                                  jstring p_szpath) {
    std::string _szPath = GetJavaString(env, p_szpath);

    const GameList::Entry *entry;
    entry = GameList::GetEntryForPath(_szPath.c_str());

    std::string ret;
    ret.append(entry->title);
    ret.append("|");
    ret.append(entry->serial);
    ret.append("|");
    ret.append(StringUtil::StdStringFromFormat("%s (%08X)", entry->serial.c_str(), entry->crc));

    return env->NewStringUTF(ret.c_str());
}

extern "C"
JNIEXPORT jstring JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_getGameSerial(JNIEnv *env, jclass clazz) {
    std::string ret = VMManager::GetDiscSerial();
    return env->NewStringUTF(ret.c_str());
}

extern "C"
JNIEXPORT jfloat JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_getFPS(JNIEnv *env, jclass clazz) {
    return (jfloat)PerformanceMetrics::GetFPS();
}

extern "C"
JNIEXPORT jstring JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_getPauseGameTitle(JNIEnv *env, jclass clazz) {
    std::string ret = VMManager::GetTitle(true);
    return env->NewStringUTF(ret.c_str());
}

extern "C"
JNIEXPORT jstring JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_getPauseGameSerial(JNIEnv *env, jclass clazz) {
    std::string ret = StringUtil::StdStringFromFormat("%s (%08X)", VMManager::GetDiscSerial().c_str(), VMManager::GetDiscCRC());
    return env->NewStringUTF(ret.c_str());
}


extern "C"
JNIEXPORT void JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_setPadVibration(JNIEnv *env, jclass clazz,
                                                     jboolean p_isOnOff) {
}


extern "C" JNIEXPORT void JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_setPadButton(JNIEnv *env, jclass clazz,
                                                  jint p_key, jint p_range, jboolean p_keyPressed) {
    PadDualshock2::Inputs _key;
    switch (p_key) {
        case 19: _key = PadDualshock2::Inputs::PAD_UP; break;
        case 22: _key = PadDualshock2::Inputs::PAD_RIGHT; break;
        case 20: _key = PadDualshock2::Inputs::PAD_DOWN; break;
        case 21: _key = PadDualshock2::Inputs::PAD_LEFT; break;
        case 100: _key = PadDualshock2::Inputs::PAD_TRIANGLE; break;
        case 97: _key = PadDualshock2::Inputs::PAD_CIRCLE; break;
        case 96: _key = PadDualshock2::Inputs::PAD_CROSS; break;
        case 99: _key = PadDualshock2::Inputs::PAD_SQUARE; break;
        case 109: _key = PadDualshock2::Inputs::PAD_SELECT; break;
        case 108: _key = PadDualshock2::Inputs::PAD_START; break;
        case 102: _key = PadDualshock2::Inputs::PAD_L1; break;
        case 104: _key = PadDualshock2::Inputs::PAD_L2; break;
        case 103: _key = PadDualshock2::Inputs::PAD_R1; break;
        case 105: _key = PadDualshock2::Inputs::PAD_R2; break;
        case 106: _key = PadDualshock2::Inputs::PAD_L3; break;
        case 107: _key = PadDualshock2::Inputs::PAD_R3; break;
        case 110: _key = PadDualshock2::Inputs::PAD_L_UP; break;
        case 111: _key = PadDualshock2::Inputs::PAD_L_RIGHT; break;
        case 112: _key = PadDualshock2::Inputs::PAD_L_DOWN; break;
        case 113: _key = PadDualshock2::Inputs::PAD_L_LEFT; break;
        case 120: _key = PadDualshock2::Inputs::PAD_R_UP; break;
        case 121: _key = PadDualshock2::Inputs::PAD_R_RIGHT; break;
        case 122: _key = PadDualshock2::Inputs::PAD_R_DOWN; break;
        case 123: _key = PadDualshock2::Inputs::PAD_R_LEFT; break;
        default: _key = PadDualshock2::Inputs::PAD_CROSS ; break;
    }

    Pad::SetControllerState(0, static_cast<u32>(_key), p_keyPressed ? 1.0 : 0.0);
}

extern "C" JNIEXPORT void JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_resetKeyStatus(JNIEnv *env, jclass clazz) {
}

extern "C"
JNIEXPORT void JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_setEnableCheats(JNIEnv *env, jclass clazz,
                                                     jboolean p_isonoff) {
}

extern "C"
JNIEXPORT void JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_setAspectRatio(JNIEnv *env, jclass clazz,
                                                    jint p_type) {
}

extern "C"
JNIEXPORT void JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_speedhackLimitermode(JNIEnv *env, jclass clazz,
                                                          jint p_value) {
}

extern "C"
JNIEXPORT void JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_speedhackEecyclerate(JNIEnv *env, jclass clazz,
                                                          jint p_value) {
}

extern "C"
JNIEXPORT void JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_speedhackEecycleskip(JNIEnv *env, jclass clazz,
                                                          jint p_value) {
}

extern "C"
JNIEXPORT void JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_renderUpscalemultiplier(JNIEnv *env, jclass clazz,
                                                             jfloat p_value) {
}

extern "C"
JNIEXPORT void JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_renderMipmap(JNIEnv *env, jclass clazz,
                                                  jint p_value) {
}

extern "C"
JNIEXPORT void JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_renderHalfpixeloffset(JNIEnv *env, jclass clazz,
                                                           jint p_value) {
}

extern "C"
JNIEXPORT void JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_renderPreloading(JNIEnv *env, jclass clazz,
                                                      jint p_value) {
}

extern "C"
JNIEXPORT void JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_renderGpu(JNIEnv *env, jclass clazz,
                                               jint p_value) {
    EmuConfig.GS.Renderer = static_cast<GSRendererType>(p_value);
    if(MTGS::IsOpen()) {
        MTGS::ApplySettings();
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_onNativeSurfaceCreated(JNIEnv *env, jclass clazz) {
}

extern "C"
JNIEXPORT void JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_onNativeSurfaceChanged(JNIEnv *env, jclass clazz,
                                                            jobject p_surface, jint p_width, jint p_height) {
    if(s_window) {
        ANativeWindow_release(s_window);
        s_window = nullptr;
    }

    if(p_surface != nullptr) {
        s_window = ANativeWindow_fromSurface(env, p_surface);
    }

    if(p_width > 0 && p_height > 0) {
        s_window_width = p_width;
        s_window_height = p_height;
        if(MTGS::IsOpen()) {
            MTGS::UpdateDisplayWindow();
        }
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_onNativeSurfaceDestroyed(JNIEnv *env, jclass clazz) {
    if(s_window) {
        ANativeWindow_release(s_window);
        s_window = nullptr;
    }
}


std::optional<WindowInfo> Host::AcquireRenderWindow(bool recreate_window)
{
    float _fScale = 1.0;
    if (s_window_width > 0 && s_window_height > 0) {
        int _nSize = s_window_width;
        if (s_window_width <= s_window_height) {
            _nSize = s_window_height;
        }
        _fScale = (float)_nSize / 800.0f;
    }
    ////
    WindowInfo _windowInfo;
    memset(&_windowInfo, 0, sizeof(_windowInfo));
    _windowInfo.type = WindowInfo::Type::Android;
    _windowInfo.surface_width = s_window_width;
    _windowInfo.surface_height = s_window_height;
    _windowInfo.surface_scale = _fScale;
    _windowInfo.window_handle = s_window;

    return _windowInfo;
}

void Host::ReleaseRenderWindow() {

}

static s32 s_loop_count = 1;

// Owned by the GS thread.
static u32 s_dump_frame_number = 0;
static u32 s_loop_number = s_loop_count;
static double s_last_internal_draws = 0;
static double s_last_draws = 0;
static double s_last_render_passes = 0;
static double s_last_barriers = 0;
static double s_last_copies = 0;
static double s_last_uploads = 0;
static double s_last_readbacks = 0;
static u64 s_total_internal_draws = 0;
static u64 s_total_draws = 0;
static u64 s_total_render_passes = 0;
static u64 s_total_barriers = 0;
static u64 s_total_copies = 0;
static u64 s_total_uploads = 0;
static u64 s_total_readbacks = 0;
static u32 s_total_frames = 0;
static u32 s_total_drawn_frames = 0;

void Host::BeginPresentFrame() {
    if (GSIsHardwareRenderer())
    {
        const u32 last_draws = s_total_internal_draws;
        const u32 last_uploads = s_total_uploads;

        static constexpr auto update_stat = [](GSPerfMon::counter_t counter, u64& dst, double& last) {
            // perfmon resets every 30 frames to zero
            const double val = g_perfmon.GetCounter(counter);
            dst += static_cast<u64>((val < last) ? val : (val - last));
            last = val;
        };

        update_stat(GSPerfMon::Draw, s_total_internal_draws, s_last_internal_draws);
        update_stat(GSPerfMon::DrawCalls, s_total_draws, s_last_draws);
        update_stat(GSPerfMon::RenderPasses, s_total_render_passes, s_last_render_passes);
        update_stat(GSPerfMon::Barriers, s_total_barriers, s_last_barriers);
        update_stat(GSPerfMon::TextureCopies, s_total_copies, s_last_copies);
        update_stat(GSPerfMon::TextureUploads, s_total_uploads, s_last_uploads);
        update_stat(GSPerfMon::Readbacks, s_total_readbacks, s_last_readbacks);

        const bool idle_frame = s_total_frames && (last_draws == s_total_internal_draws && last_uploads == s_total_uploads);

        if (!idle_frame)
            s_total_drawn_frames++;

        s_total_frames++;

        std::atomic_thread_fence(std::memory_order_release);
    }
}

void Host::OnGameChanged(const std::string& title, const std::string& elf_override, const std::string& disc_path,
                         const std::string& disc_serial, u32 disc_crc, u32 current_crc) {
}

void Host::PumpMessagesOnCPUThread() {
}

int FileSystem::OpenFDFileContent(const char* filename)
{
    auto *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
    if(env == nullptr) {
        return -1;
    }
    jclass NativeApp = env->FindClass("kr/co/iefriends/pcsx2/NativeApp");
    jmethodID openContentUri = env->GetStaticMethodID(NativeApp, "openContentUri", "(Ljava/lang/String;)I");

    jstring j_filename = env->NewStringUTF(filename);
    int fd = env->CallStaticIntMethod(NativeApp, openContentUri, j_filename);
    return fd;
}


extern "C"
JNIEXPORT jboolean JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_runVMThread(JNIEnv *env, jclass clazz,
                                                 jstring p_szpath) {
    std::string _szPath = GetJavaString(env, p_szpath);

    /////////////////////////////

    s_execute_exit = false;

//    const char* error;
//    if (!VMManager::PerformEarlyHardwareChecks(&error)) {
//        return false;
//    }

    // fast_boot : (false:bios->game, true:game)
    VMBootParameters boot_params;
    boot_params.filename = _szPath;

    if (!VMManager::Internal::CPUThreadInitialize()) {
        VMManager::Internal::CPUThreadShutdown();
    }

    VMManager::ApplySettings();
    GSDumpReplayer::SetIsDumpRunner(false);

    if (VMManager::Initialize(boot_params))
    {
        VMState _vmState = VMState::Running;
        VMManager::SetState(_vmState);
        ////
        while (true) {
            _vmState = VMManager::GetState();
            if (_vmState == VMState::Stopping || _vmState == VMState::Shutdown) {
                break;
            } else if (_vmState == VMState::Running) {
                s_execute_exit = false;
                VMManager::Execute();
                s_execute_exit = true;
            } else {
                usleep(250000);
            }
        }
        ////
        VMManager::Shutdown(false);
    }
    ////
    VMManager::Internal::CPUThreadShutdown();

    return true;
}

extern "C"
JNIEXPORT void JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_pause(JNIEnv *env, jclass clazz) {
    std::thread([] {
        VMManager::SetPaused(true);
    }).detach();
}

extern "C"
JNIEXPORT void JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_resume(JNIEnv *env, jclass clazz) {
    std::thread([] {
        VMManager::SetPaused(false);
    }).detach();
}

extern "C"
JNIEXPORT void JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_shutdown(JNIEnv *env, jclass clazz) {
    std::thread([] {
        VMManager::SetState(VMState::Stopping);
    }).detach();
}


extern "C"
JNIEXPORT jboolean JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_saveStateToSlot(JNIEnv *env, jclass clazz, jint p_slot) {
    if (!VMManager::HasValidVM()) {
        return false;
    }

    std::future<bool> ret = std::async([p_slot]
    {
       if(VMManager::GetDiscCRC() != 0) {
           if(VMManager::GetState() != VMState::Paused) {
               VMManager::SetPaused(true);
           }

           // wait 5 sec
           for (int i = 0; i < 5; ++i) {
               if (s_execute_exit) {
                   if(VMManager::SaveStateToSlot(p_slot, false)) {
                       return true;
                   }
                   break;
               }
               sleep(1);
           }
       }
       return false;

    });

    return ret.get();
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_loadStateFromSlot(JNIEnv *env, jclass clazz, jint p_slot) {
    if (!VMManager::HasValidVM()) {
        return false;
    }

    std::future<bool> ret = std::async([p_slot]
    {
       u32 _crc = VMManager::GetDiscCRC();
       if(_crc != 0) {
           if (VMManager::HasSaveStateInSlot(VMManager::GetDiscSerial().c_str(), _crc, p_slot)) {
               if(VMManager::GetState() != VMState::Paused) {
                   VMManager::SetPaused(true);
               }

               // wait 5 sec
               for (int i = 0; i < 5; ++i) {
                   if (s_execute_exit) {
                       if(VMManager::LoadStateFromSlot(p_slot)) {
                           return true;
                       }
                       break;
                   }
                   sleep(1);
               }
           }
       }
       return false;
    });

    return ret.get();
}

extern "C"
JNIEXPORT jstring JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_getGamePathSlot(JNIEnv *env, jclass clazz, jint p_slot) {
    std::string _filename = VMManager::GetSaveStateFileName(VMManager::GetDiscSerial().c_str(), VMManager::GetDiscCRC(), p_slot);
    if(!_filename.empty()) {
        return env->NewStringUTF(_filename.c_str());
    }
    return nullptr;
}

extern "C"
JNIEXPORT jbyteArray JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_getImageSlot(JNIEnv *env, jclass clazz, jint p_slot) {
    jbyteArray retArr = nullptr;

    std::string _filename = VMManager::GetSaveStateFileName(VMManager::GetDiscSerial().c_str(), VMManager::GetDiscCRC(), p_slot);
    if(!_filename.empty())
    {
        zip_error_t ze = {};
        auto zf = zip_open_managed(_filename.c_str(), ZIP_RDONLY, &ze);
        if (zf) {
            auto zff = zip_fopen_managed(zf.get(), "Screenshot.png", 0);
            if(zff) {
                std::optional<std::vector<u8>> optdata(ReadBinaryFileInZip(zff.get()));
                if (optdata.has_value()) {
                    std::vector<u8> vec = std::move(optdata.value());
                    ////
                    auto length = static_cast<jsize>(vec.size());
                    retArr = env->NewByteArray(length);
                    if (retArr != nullptr) {
                        env->SetByteArrayRegion(retArr, 0, length,
                                                reinterpret_cast<const jbyte *>(vec.data()));
                    }
                }
            }
        }
    }

    return retArr;
}


void Host::CommitBaseSettingChanges()
{
    // nothing to save, we're all in memory
}

void Host::LoadSettings(SettingsInterface& si, std::unique_lock<std::mutex>& lock)
{
}

void Host::CheckForSettingsChanges(const Pcsx2Config& old_config)
{
}

bool Host::RequestResetSettings(bool folders, bool core, bool controllers, bool hotkeys, bool ui)
{
    // not running any UI, so no settings requests will come in
    return false;
}

void Host::SetDefaultUISettings(SettingsInterface& si)
{
    // nothing
}

std::unique_ptr<ProgressCallback> Host::CreateHostProgressCallback()
{
    return nullptr;
}

void Host::ReportErrorAsync(const std::string_view title, const std::string_view message)
{
    if (!title.empty() && !message.empty())
        ERROR_LOG("ReportErrorAsync: {}: {}", title, message);
    else if (!message.empty())
        ERROR_LOG("ReportErrorAsync: {}", message);
}

bool Host::ConfirmMessage(const std::string_view title, const std::string_view message)
{
    if (!title.empty() && !message.empty())
        ERROR_LOG("ConfirmMessage: {}: {}", title, message);
    else if (!message.empty())
        ERROR_LOG("ConfirmMessage: {}", message);

    return true;
}

void Host::OpenURL(const std::string_view url)
{
    // noop
}

bool Host::CopyTextToClipboard(const std::string_view text)
{
    return false;
}

void Host::BeginTextInput()
{
    // noop
}

void Host::EndTextInput()
{
    // noop
}

std::optional<WindowInfo> Host::GetTopLevelWindowInfo()
{
    return std::nullopt;
}

void Host::OnInputDeviceConnected(const std::string_view identifier, const std::string_view device_name)
{
}

void Host::OnInputDeviceDisconnected(const InputBindingKey key, const std::string_view identifier)
{
}

void Host::SetMouseMode(bool relative_mode, bool hide_cursor)
{
}

void Host::RequestResizeHostDisplay(s32 width, s32 height)
{
}

void Host::OnVMStarting()
{
}

void Host::OnVMStarted()
{
}

void Host::OnVMDestroyed()
{
}

void Host::OnVMPaused()
{
}

void Host::OnVMResumed()
{
}

void Host::OnPerformanceMetricsUpdated()
{
}

void Host::OnSaveStateLoading(const std::string_view filename)
{
}

void Host::OnSaveStateLoaded(const std::string_view filename, bool was_successful)
{
}

void Host::OnSaveStateSaved(const std::string_view filename)
{
}

void Host::RunOnCPUThread(std::function<void()> function, bool block /* = false */)
{
    pxFailRel("Not implemented");
}

void Host::RefreshGameListAsync(bool invalidate_cache)
{
}

void Host::CancelGameListRefresh()
{
}

bool Host::IsFullscreen()
{
    return false;
}

void Host::SetFullscreen(bool enabled)
{
}

void Host::OnCaptureStarted(const std::string& filename)
{
}

void Host::OnCaptureStopped()
{
}

void Host::RequestExitApplication(bool allow_confirm)
{
}

void Host::RequestExitBigPicture()
{
}

void Host::RequestVMShutdown(bool allow_confirm, bool allow_save_state, bool default_save_state)
{
    VMManager::SetState(VMState::Stopping);
}

void Host::OnAchievementsLoginSuccess(const char* username, u32 points, u32 sc_points, u32 unread_messages)
{
    // noop
}

void Host::OnAchievementsLoginRequested(Achievements::LoginRequestReason reason)
{
    // noop
}

void Host::OnAchievementsHardcoreModeChanged(bool enabled)
{
    // noop
}

void Host::OnAchievementsRefreshed()
{
    // noop
}

void Host::OnCoverDownloaderOpenRequested()
{
    // noop
}

void Host::OnCreateMemoryCardOpenRequested()
{
    // noop
}

bool Host::ShouldPreferHostFileSelector()
{
    return false;
}

void Host::OpenHostFileSelectorAsync(std::string_view title, bool select_directory, FileSelectorCallback callback,
                                     FileSelectorFilters filters, std::string_view initial_directory)
{
    callback(std::string());
}

std::optional<u32> InputManager::ConvertHostKeyboardStringToCode(const std::string_view str)
{
    return std::nullopt;
}

std::optional<std::string> InputManager::ConvertHostKeyboardCodeToString(u32 code)
{
    return std::nullopt;
}

const char* InputManager::ConvertHostKeyboardCodeToIcon(u32 code)
{
    return nullptr;
}

s32 Host::Internal::GetTranslatedStringImpl(
        const std::string_view context, const std::string_view msg, char* tbuf, size_t tbuf_space)
{
    if (msg.size() > tbuf_space)
        return -1;
    else if (msg.empty())
        return 0;

    std::memcpy(tbuf, msg.data(), msg.size());
    return static_cast<s32>(msg.size());
}

std::string Host::TranslatePluralToString(const char* context, const char* msg, const char* disambiguation, int count)
{
    TinyString count_str = TinyString::from_format("{}", count);

    std::string ret(msg);
    for (;;)
    {
        std::string::size_type pos = ret.find("%n");
        if (pos == std::string::npos)
            break;

        ret.replace(pos, pos + 2, count_str.view());
    }

    return ret;
}

void Host::ReportInfoAsync(const std::string_view title, const std::string_view message)
{
}

bool Host::LocaleCircleConfirm()
{
    return false;
}

bool Host::InNoGUIMode()
{
    return false;
}
