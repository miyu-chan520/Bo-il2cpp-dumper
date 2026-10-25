//
// Created by Perfare on 2020/7/4.
// Modernized with Toast notifications, memory anonymous remapping, and anti-crash lifecycle.
//

#include "hack.h"
#include "il2cpp_dump.h"
#include "log.h"
#include "xdl.h"
#include <cstring>
#include <cstdio>
#include <unistd.h>
#include <sys/system_properties.h>
#include <dlfcn.h>
#include <jni.h>
#include <thread>
#include <sys/mman.h>
#include <linux/unistd.h>
#include <array>
#include <vector>
#include <cinttypes>
#include <cstdarg>
#include <mutex>
#include <chrono>

JavaVM *get_jvm() {
    static JavaVM *s_jvm = nullptr;
    if (s_jvm) return s_jvm;
    void *libart = dlopen("libart.so", RTLD_NOW);
    if (!libart) return nullptr;
    auto JNI_GetCreatedJavaVMs = (jint (*)(JavaVM **, jsize, jsize *)) dlsym(libart, "JNI_GetCreatedJavaVMs");
    if (!JNI_GetCreatedJavaVMs) return nullptr;
    JavaVM *vms_buf[1];
    jsize num_vms = 0;
    if (JNI_GetCreatedJavaVMs(vms_buf, 1, &num_vms) == JNI_OK && num_vms > 0) {
        s_jvm = vms_buf[0];
    }
    return s_jvm;
}

void init_log(const char *game_data_dir) {
    // Disk logging disabled per user configuration
}

void write_log(const char *fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    LOGI("%s", buf);
}

void show_toast(const std::string &message) {
    write_log("[Toast] %s", message.c_str());
    std::thread([message]() {
        pthread_setname_np(pthread_self(), "AsyncReadWorker");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        JavaVM *jvm = get_jvm();
        if (!jvm) return;
        JNIEnv *env = nullptr;
        if (jvm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) return;

        jclass at_clz = env->FindClass("android/app/ActivityThread");
        if (!at_clz) { env->ExceptionClear(); return; }
        jmethodID cur_app_mid = env->GetStaticMethodID(at_clz, "currentApplication", "()Landroid/app/Application;");
        if (!cur_app_mid) { env->ExceptionClear(); return; }
        jobject context = env->CallStaticObjectMethod(at_clz, cur_app_mid);
        if (!context) { env->ExceptionClear(); return; }

        jclass looper_clz = env->FindClass("android/os/Looper");
        if (!looper_clz) { env->ExceptionClear(); return; }
        jmethodID my_looper_mid = env->GetStaticMethodID(looper_clz, "myLooper", "()Landroid/os/Looper;");
        jobject my_looper = env->CallStaticObjectMethod(looper_clz, my_looper_mid);
        if (!my_looper) {
            jmethodID prepare_mid = env->GetStaticMethodID(looper_clz, "prepare", "()V");
            if (prepare_mid) env->CallStaticVoidMethod(looper_clz, prepare_mid);
            my_looper = env->CallStaticObjectMethod(looper_clz, my_looper_mid);
        }

        jclass toast_clz = env->FindClass("android/widget/Toast");
        if (!toast_clz) { env->ExceptionClear(); return; }
        jmethodID make_text_mid = env->GetStaticMethodID(toast_clz, "makeText", "(Landroid/content/Context;Ljava/lang/CharSequence;I)Landroid/widget/Toast;");
        if (!make_text_mid) { env->ExceptionClear(); return; }

        jstring jmsg = env->NewStringUTF(message.c_str());
        jobject toast = env->CallStaticObjectMethod(toast_clz, make_text_mid, context, jmsg, (jint) 1);
        if (toast) {
            jmethodID show_mid = env->GetMethodID(toast_clz, "show", "()V");
            if (show_mid) env->CallVoidMethod(toast, show_mid);
        }
        env->DeleteLocalRef(jmsg);

        if (my_looper) {
            jobject global_looper = env->NewGlobalRef(my_looper);
            std::thread([jvm, global_looper]() {
                pthread_setname_np(pthread_self(), "AsyncLooperTimer");
                std::this_thread::sleep_for(std::chrono::milliseconds(3500));
                JNIEnv *qenv = nullptr;
                if (jvm->AttachCurrentThread(&qenv, nullptr) == JNI_OK && qenv) {
                    jclass lclz = qenv->FindClass("android/os/Looper");
                    if (lclz) {
                        jmethodID qmid = qenv->GetMethodID(lclz, "quitSafely", "()V");
                        if (qmid) qenv->CallVoidMethod(global_looper, qmid);
                    }
                    qenv->DeleteGlobalRef(global_looper);
                }
            }).detach();

            jmethodID loop_mid = env->GetStaticMethodID(looper_clz, "loop", "()V");
            if (loop_mid) env->CallStaticVoidMethod(looper_clz, loop_mid);
        }
    }).detach();
}

void hide_self_from_maps() {
    Dl_info info;
    if (!dladdr((void*)hide_self_from_maps, &info) || !info.dli_fname) return;
    std::string my_path = info.dli_fname;

    FILE *fp = fopen("/proc/self/maps", "r");
    if (!fp) return;

    struct MapSeg {
        uintptr_t start;
        uintptr_t end;
        int prot;
    };
    std::vector<MapSeg> segs;
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, my_path.c_str()) || strstr(line, "zygisk_il2cppdumper")) {
            uintptr_t start = 0, end = 0;
            char perms[5] = {0};
            if (sscanf(line, "%" SCNxPTR "-%" SCNxPTR " %4s", &start, &end, perms) >= 3) {
                int prot = 0;
                if (perms[0] == 'r') prot |= PROT_READ;
                if (perms[1] == 'w') prot |= PROT_WRITE;
                if (perms[2] == 'x') prot |= PROT_EXEC;
                segs.push_back({start, end, prot});
            }
        }
    }
    fclose(fp);

    for (const auto &seg : segs) {
        size_t len = seg.end - seg.start;
        void *backup = malloc(len);
        if (!backup) continue;
        memcpy(backup, (void*)seg.start, len);

        // Replace file-backed mapping with anonymous memory
        void *anon = mmap((void*)seg.start, len, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        if (anon != MAP_FAILED) {
            memcpy((void*)seg.start, backup, len);
            mprotect((void*)seg.start, len, seg.prot);
        }
        free(backup);
    }
}

void hack_start(const char *game_data_dir) {
    pthread_setname_np(pthread_self(), "UnityGfxDevice");
    write_log("[+] Zygisk-Il2CppDumper attached. Data dir: %s", game_data_dir);

    // Hide our module mappings from /proc/self/maps to bypass anti-cheat scanners
    hide_self_from_maps();
    write_log("[+] Self-hiding completed: module mappings converted to anonymous memory");

    write_log("[+] Waiting for libil2cpp.so to load...");

    bool load = false;
    for (int i = 0; i < 180; i++) {
        void *handle = xdl_open("libil2cpp.so", 0);
        if (handle) {
            load = true;
            write_log("[+] Found libil2cpp.so: %p (waited %d seconds)", handle, i);

            // Wait 10 seconds for UnityMain to finish il2cpp_init and stabilize Boehm GC
            write_log("[+] Waiting 10 seconds for Unity engine and GC initialization to complete...");
            sleep(10);

            il2cpp_api_init(handle);
            il2cpp_dump("/sdcard/Bo_dump");

            // DO NOT call xdl_close(handle) to prevent game crash
            write_log("[+] Dump finished. Keeping thread dormant to preserve game stability.");
            while (true) {
                sleep(3600);
            }
            break;
        } else {
            sleep(1);
        }
    }
    if (!load) {
        std::string err = "libil2cpp.so not found after 180s timeout";
        write_log("[-] %s", err.c_str());
        show_toast(std::string("[Il2CppDumper] FAIL: ") + err);
    }
}

std::string GetLibDir(JavaVM *vms) {
    JNIEnv *env = nullptr;
    vms->AttachCurrentThread(&env, nullptr);
    jclass activity_thread_clz = env->FindClass("android/app/ActivityThread");
    if (activity_thread_clz != nullptr) {
        jmethodID currentApplicationId = env->GetStaticMethodID(activity_thread_clz,
                                                                "currentApplication",
                                                                "()Landroid/app/Application;");
        if (currentApplicationId) {
            jobject application = env->CallStaticObjectMethod(activity_thread_clz,
                                                              currentApplicationId);
            jclass application_clazz = env->GetObjectClass(application);
            if (application_clazz) {
                jmethodID get_application_info = env->GetMethodID(application_clazz,
                                                                  "getApplicationInfo",
                                                                  "()Landroid/content/pm/ApplicationInfo;");
                if (get_application_info) {
                    jobject application_info = env->CallObjectMethod(application,
                                                                     get_application_info);
                    jfieldID native_library_dir_id = env->GetFieldID(
                            env->GetObjectClass(application_info), "nativeLibraryDir",
                            "Ljava/lang/String;");
                    if (native_library_dir_id) {
                        auto native_library_dir_jstring = (jstring) env->GetObjectField(
                                application_info, native_library_dir_id);
                        auto path = env->GetStringUTFChars(native_library_dir_jstring, nullptr);
                        LOGI("lib dir %s", path);
                        std::string lib_dir(path);
                        env->ReleaseStringUTFChars(native_library_dir_jstring, path);
                        return lib_dir;
                    } else {
                        LOGE("nativeLibraryDir not found");
                    }
                } else {
                    LOGE("getApplicationInfo not found");
                }
            } else {
                LOGE("application class not found");
            }
        } else {
            LOGE("currentApplication not found");
        }
    } else {
        LOGE("ActivityThread not found");
    }
    return {};
}

static std::string GetNativeBridgeLibrary() {
    auto value = std::array<char, PROP_VALUE_MAX>();
    __system_property_get("ro.dalvik.vm.native.bridge", value.data());
    return {value.data()};
}

struct NativeBridgeCallbacks {
    uint32_t version;
    void *initialize;

    void *(*loadLibrary)(const char *libpath, int flag);

    void *(*getTrampoline)(void *handle, const char *name, const char *shorty, uint32_t len);

    void *isSupported;
    void *getAppEnv;
    void *isCompatibleWith;
    void *getSignalHandler;
    void *unloadLibrary;
    void *getError;
    void *isPathSupported;
    void *initAnonymousNamespace;
    void *createNamespace;
    void *linkNamespaces;

    void *(*loadLibraryExt)(const char *libpath, int flag, void *ns);
};

bool NativeBridgeLoad(const char *game_data_dir, int api_level, void *data, size_t length) {
    sleep(5);

    auto libart = dlopen("libart.so", RTLD_NOW);
    auto JNI_GetCreatedJavaVMs = (jint (*)(JavaVM **, jsize, jsize *)) dlsym(libart,
                                                                             "JNI_GetCreatedJavaVMs");
    LOGI("JNI_GetCreatedJavaVMs %p", JNI_GetCreatedJavaVMs);
    JavaVM *vms_buf[1];
    JavaVM *vms;
    jsize num_vms;
    jint status = JNI_GetCreatedJavaVMs(vms_buf, 1, &num_vms);
    if (status == JNI_OK && num_vms > 0) {
        vms = vms_buf[0];
    } else {
        LOGE("GetCreatedJavaVMs error");
        return false;
    }

    auto lib_dir = GetLibDir(vms);
    if (lib_dir.empty()) {
        LOGE("GetLibDir error");
        return false;
    }
    if (lib_dir.find("/lib/x86") != std::string::npos) {
        LOGI("no need NativeBridge");
        munmap(data, length);
        return false;
    }

    auto nb = dlopen("libhoudini.so", RTLD_NOW);
    if (!nb) {
        auto native_bridge = GetNativeBridgeLibrary();
        LOGI("native bridge: %s", native_bridge.data());
        nb = dlopen(native_bridge.data(), RTLD_NOW);
    }
    if (nb) {
        LOGI("nb %p", nb);
        auto callbacks = (NativeBridgeCallbacks *) dlsym(nb, "NativeBridgeItf");
        if (callbacks) {
            LOGI("NativeBridgeLoadLibrary %p", callbacks->loadLibrary);
            LOGI("NativeBridgeLoadLibraryExt %p", callbacks->loadLibraryExt);
            LOGI("NativeBridgeGetTrampoline %p", callbacks->getTrampoline);

            int fd = syscall(__NR_memfd_create, "anon", MFD_CLOEXEC);
            ftruncate(fd, (off_t) length);
            void *mem = mmap(nullptr, length, PROT_WRITE, MAP_SHARED, fd, 0);
            memcpy(mem, data, length);
            munmap(mem, length);
            munmap(data, length);
            char path[PATH_MAX];
            snprintf(path, PATH_MAX, "/proc/self/fd/%d", fd);
            LOGI("arm path %s", path);

            void *arm_handle;
            if (api_level >= 26) {
                arm_handle = callbacks->loadLibraryExt(path, RTLD_NOW, (void *) 3);
            } else {
                arm_handle = callbacks->loadLibrary(path, RTLD_NOW);
            }
            if (arm_handle) {
                LOGI("arm handle %p", arm_handle);
                auto init = (void (*)(JavaVM *, void *)) callbacks->getTrampoline(arm_handle,
                                                                                  "JNI_OnLoad",
                                                                                  nullptr, 0);
                LOGI("JNI_OnLoad %p", init);
                init(vms, (void *) game_data_dir);
                return true;
            }
            close(fd);
        }
    }
    return false;
}

void hack_prepare(const char *game_data_dir, void *data, size_t length) {
    pthread_setname_np(pthread_self(), "JobWorker 0");
    LOGI("hack thread: %d", gettid());
    int api_level = android_get_device_api_level();
    LOGI("api level: %d", api_level);

#if defined(__i386__) || defined(__x86_64__)
    if (!NativeBridgeLoad(game_data_dir, api_level, data, length)) {
#endif
        hack_start(game_data_dir);
#if defined(__i386__) || defined(__x86_64__)
    }
#endif
}

#if defined(__arm__) || defined(__aarch64__)

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    auto game_data_dir = (const char *) reserved;
    std::thread hack_thread(hack_start, game_data_dir);
    hack_thread.detach();
    return JNI_VERSION_1_6;
}

#endif
