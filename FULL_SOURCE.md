# Zygisk-Il2CppDumper - Complete Source Code

Project Location: `C:\Users\doant\.gemini\antigravity\scratch\Zygisk-Il2CppDumper`

---

## 1. `module/src/main/cpp/main.cpp`
```cpp
#include <cstring>
#include <thread>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cinttypes>
#include <sys/system_properties.h>
#include "hack.h"
#include "zygisk.hpp"
#include "game.h"
#include "log.h"

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;

class MyModule : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        auto package_name = env->GetStringUTFChars(args->nice_name, nullptr);
        auto app_data_dir = env->GetStringUTFChars(args->app_data_dir, nullptr);
        preSpecialize(package_name, app_data_dir);
        env->ReleaseStringUTFChars(args->nice_name, package_name);
        env->ReleaseStringUTFChars(args->app_data_dir, app_data_dir);
    }

    void postAppSpecialize(const AppSpecializeArgs *) override {
        if (enable_hack) {
            std::thread hack_thread(hack_prepare, game_data_dir, data, length);
            hack_thread.detach();
        }
    }

private:
    Api *api;
    JNIEnv *env;
    bool enable_hack;
    char *game_data_dir;
    void *data;
    size_t length;

    bool is_dumper_enabled() {
        if (access("/data/local/tmp/il2cppdumper_disabled", F_OK) == 0) {
            return false;
        }
        char prop[PROP_VALUE_MAX] = {0};
        __system_property_get("persist.il2cppdumper.enabled", prop);
        if (strlen(prop) > 0 && strcmp(prop, "0") == 0) {
            return false;
        }
        return true;
    }

    void preSpecialize(const char *package_name, const char *app_data_dir) {
        if (!is_dumper_enabled()) {
            LOGI("Zygisk-Il2CppDumper is globally DISABLED by user switch");
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        bool match = false;
        if (package_name) {
            if (strcmp(package_name, GamePackageName) == 0) {
                match = true;
            } else {
                char prop[PROP_VALUE_MAX] = {0};
                __system_property_get("persist.il2cppdumper.package", prop);
                if (strlen(prop) > 0 && strcmp(package_name, prop) == 0) {
                    match = true;
                }
            }
        }
        if (match) {
            LOGI("detect game: %s", package_name);
            enable_hack = true;
            game_data_dir = new char[strlen(app_data_dir) + 1];
            strcpy(game_data_dir, app_data_dir);

#if defined(__i386__)
            auto path = "zygisk/armeabi-v7a.so";
#endif
#if defined(__x86_64__)
            auto path = "zygisk/arm64-v8a.so";
#endif
#if defined(__i386__) || defined(__x86_64__)
            int dirfd = api->getModuleDir();
            int fd = openat(dirfd, path, O_RDONLY);
            if (fd != -1) {
                struct stat sb{};
                fstat(fd, &sb);
                length = sb.st_size;
                data = mmap(nullptr, length, PROT_READ, MAP_PRIVATE, fd, 0);
                close(fd);
            } else {
                LOGW("Unable to open arm file");
            }
#endif
        } else {
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
        }
    }
};

REGISTER_ZYGISK_MODULE(MyModule)
```

---

## 2. `module/src/main/cpp/game.h`
```cpp
#ifndef ZYGISK_IL2CPPDUMPER_GAME_H
#define ZYGISK_IL2CPPDUMPER_GAME_H

#define GamePackageName "com.dts.freefireth"

#endif //ZYGISK_IL2CPPDUMPER_GAME_H
```

---

## 3. `module/src/main/cpp/log.h`
```cpp
#ifndef ZYGISK_IL2CPPDUMPER_LOG_H
#define ZYGISK_IL2CPPDUMPER_LOG_H

#include <android/log.h>

#define LOG_TAG "Il2CppDumper"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

#endif //ZYGISK_IL2CPPDUMPER_LOG_H
```

---

## 4. `module/src/main/cpp/hack.h`
```cpp
#ifndef ZYGISK_IL2CPPDUMPER_HACK_H
#define ZYGISK_IL2CPPDUMPER_HACK_H

#include <stddef.h>
#include <string>

void hack_prepare(const char *game_data_dir, void *data, size_t length);
void hide_self_from_maps();
void show_toast(const std::string &message);
void init_log(const char *game_data_dir);
void write_log(const char *fmt, ...);

#endif //ZYGISK_IL2CPPDUMPER_HACK_H
```

---

## 5. `module/src/main/cpp/hack.cpp`
```cpp
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

    hide_self_from_maps();
    write_log("[+] Self-hiding completed: module mappings converted to anonymous memory");

    write_log("[+] Waiting for libil2cpp.so to load...");

    bool load = false;
    for (int i = 0; i < 180; i++) {
        void *handle = xdl_open("libil2cpp.so", 0);
        if (handle) {
            load = true;
            write_log("[+] Found libil2cpp.so: %p (waited %d seconds)", handle, i);

            write_log("[+] Waiting 10 seconds for Unity engine and GC initialization to complete...");
            sleep(10);

            il2cpp_api_init(handle);
            il2cpp_dump("/sdcard/Bo_dump");

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
```

---

## 6. `module/src/main/cpp/il2cpp_dump.h`
```cpp
#ifndef ZYGISK_IL2CPPDUMPER_IL2CPP_DUMP_H
#define ZYGISK_IL2CPPDUMPER_IL2CPP_DUMP_H

void il2cpp_api_init(void *handle);

void il2cpp_dump(const char *outDir);

#endif //ZYGISK_IL2CPPDUMPER_IL2CPP_DUMP_H
```

---

## 7. `module/src/main/cpp/il2cpp_dump.cpp`
```cpp
#include "il2cpp_dump.h"
#include <dlfcn.h>
#include <cstdlib>
#include <cstring>
#include <cinttypes>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unordered_set>
#include "hack.h"
#include "xdl.h"
#include "log.h"
#include "il2cpp-tabledefs.h"
#include "il2cpp-class.h"

#define DO_API(r, n, p) r (*n) p

#include "il2cpp-api-functions.h"

#undef DO_API

static uint64_t il2cpp_base = 0;
static Il2CppThread *s_attached_thread = nullptr;

struct Il2CppGlobalMetadataHeader {
    int32_t magic;
    int32_t version;
    int32_t stringLiteralOffset;
    int32_t stringLiteralCount;
    int32_t stringLiteralDataOffset;
    int32_t stringLiteralDataCount;
    int32_t stringOffset;
    int32_t stringCount;
};

struct Il2CppStringLiteralEntry {
    uint32_t length;
    int32_t dataIndex;
};

static std::string json_escape(const char *str, size_t len = 0) {
    if (!str) return "";
    std::string out;
    size_t count = len ? len : strlen(str);
    for (size_t i = 0; i < count; ++i) {
        if (str[i] == '\"') out += "\\\"";
        else if (str[i] == '\\') out += "\\\\";
        else if (str[i] == '\n') out += "\\n";
        else if (str[i] == '\r') out += "\\r";
        else if (str[i] == '\t') out += "\\t";
        else if ((unsigned char)str[i] >= 32 && (unsigned char)str[i] <= 126) out += str[i];
        else {
            char ubuf[8];
            snprintf(ubuf, sizeof(ubuf), "\\u%04x", (unsigned char)str[i]);
            out += ubuf;
        }
    }
    return out;
}

void init_il2cpp_api(void *handle) {
#define DO_API(r, n, p) {                      \
    n = (r (*) p)xdl_sym(handle, #n, nullptr); \
    if(!n) {                                   \
        LOGW("api not found %s", #n);          \
    }                                          \
}

#include "il2cpp-api-functions.h"

#undef DO_API
}

static std::string get_type_name(const Il2CppType *type) {
    if (!type) return "object";
    if (il2cpp_type_get_name) {
        char *str = il2cpp_type_get_name(type);
        if (str) {
            std::string name(str);
            if (il2cpp_free) {
                il2cpp_free(str);
            }
            if (!name.empty() && name.back() == '&') {
                name.pop_back();
            }
            return name;
        }
    }
    auto klass = il2cpp_class_from_type(type);
    if (klass) {
        const char *kname = il2cpp_class_get_name(klass);
        if (kname) return kname;
    }
    return "object";
}

std::string get_method_modifier(uint32_t flags) {
    std::stringstream outPut;
    auto access = flags & METHOD_ATTRIBUTE_MEMBER_ACCESS_MASK;
    switch (access) {
        case METHOD_ATTRIBUTE_PRIVATE:
            outPut << "private ";
            break;
        case METHOD_ATTRIBUTE_PUBLIC:
            outPut << "public ";
            break;
        case METHOD_ATTRIBUTE_FAMILY:
            outPut << "protected ";
            break;
        case METHOD_ATTRIBUTE_ASSEM:
        case METHOD_ATTRIBUTE_FAM_AND_ASSEM:
            outPut << "internal ";
            break;
        case METHOD_ATTRIBUTE_FAM_OR_ASSEM:
            outPut << "protected internal ";
            break;
    }
    if (flags & METHOD_ATTRIBUTE_STATIC) {
        outPut << "static ";
    }
    if (flags & METHOD_ATTRIBUTE_ABSTRACT) {
        outPut << "abstract ";
        if ((flags & METHOD_ATTRIBUTE_VTABLE_LAYOUT_MASK) == METHOD_ATTRIBUTE_REUSE_SLOT) {
            outPut << "override ";
        }
    } else if (flags & METHOD_ATTRIBUTE_FINAL) {
        if ((flags & METHOD_ATTRIBUTE_VTABLE_LAYOUT_MASK) == METHOD_ATTRIBUTE_REUSE_SLOT) {
            outPut << "sealed override ";
        }
    } else if (flags & METHOD_ATTRIBUTE_VIRTUAL) {
        if ((flags & METHOD_ATTRIBUTE_VTABLE_LAYOUT_MASK) == METHOD_ATTRIBUTE_NEW_SLOT) {
            outPut << "virtual ";
        } else {
            outPut << "override ";
        }
    }
    if (flags & METHOD_ATTRIBUTE_PINVOKE_IMPL) {
        outPut << "extern ";
    }
    return outPut.str();
}

bool _il2cpp_type_is_byref(const Il2CppType *type) {
    if (!type) return false;
    auto byref = type->byref;
    if (il2cpp_type_is_byref) {
        byref = il2cpp_type_is_byref(type);
    }
    return byref;
}

std::string dump_method(Il2CppClass *klass) {
    if (!klass) return "";
    std::stringstream outPut;
    outPut << "\n\t// Methods\n";
    void *iter = nullptr;
    while (auto method = il2cpp_class_get_methods(klass, &iter)) {
        if (method->methodPointer) {
            outPut << "\t// RVA: 0x";
            outPut << std::hex << (uint64_t) method->methodPointer - il2cpp_base;
            outPut << " VA: 0x";
            outPut << std::hex << (uint64_t) method->methodPointer;
        } else {
            outPut << "\t// RVA: 0x VA: 0x0";
        }
        if (il2cpp_method_get_token) {
            auto token = il2cpp_method_get_token(method);
            if (token) {
                outPut << " Token: 0x" << std::hex << token;
            }
        }
        outPut << "\n\t";
        uint32_t iflags = 0;
        auto flags = il2cpp_method_get_flags(method, &iflags);
        outPut << get_method_modifier(flags);
        
        auto return_type = il2cpp_method_get_return_type(method);
        if (_il2cpp_type_is_byref(return_type)) {
            outPut << "ref ";
        }
        std::string ret_name = get_type_name(return_type);
        const char *m_name = il2cpp_method_get_name(method);
        outPut << ret_name << " " << (m_name ? m_name : "unknown_method") << "(";
        
        auto param_count = il2cpp_method_get_param_count(method);
        for (uint32_t i = 0; i < param_count; ++i) {
            auto param = il2cpp_method_get_param(method, i);
            if (!param) continue;
            auto attrs = param->attrs;
            if (_il2cpp_type_is_byref(param)) {
                if (attrs & PARAM_ATTRIBUTE_OUT && !(attrs & PARAM_ATTRIBUTE_IN)) {
                    outPut << "out ";
                } else if (attrs & PARAM_ATTRIBUTE_IN && !(attrs & PARAM_ATTRIBUTE_OUT)) {
                    outPut << "in ";
                } else {
                    outPut << "ref ";
                }
            } else {
                if (attrs & PARAM_ATTRIBUTE_IN) {
                    outPut << "[In] ";
                }
                if (attrs & PARAM_ATTRIBUTE_OUT) {
                    outPut << "[Out] ";
                }
            }
            std::string param_type_name = get_type_name(param);
            const char *param_name = il2cpp_method_get_param_name(method, i);
            outPut << param_type_name << " "
                   << (param_name ? param_name : "p") << ", ";
        }
        if (param_count > 0) {
            outPut.seekp(-2, outPut.cur);
        }
        outPut << ") { }\n";
    }
    return outPut.str();
}

std::string dump_property(Il2CppClass *klass) {
    if (!klass) return "";
    std::stringstream outPut;
    outPut << "\n\t// Properties\n";
    void *iter = nullptr;
    while (auto prop_const = il2cpp_class_get_properties(klass, &iter)) {
        auto prop = const_cast<PropertyInfo *>(prop_const);
        auto get = il2cpp_property_get_get_method(prop);
        auto set = il2cpp_property_get_set_method(prop);
        auto prop_name = il2cpp_property_get_name(prop);
        outPut << "\t";
        const Il2CppType *prop_type = nullptr;
        uint32_t iflags = 0;
        if (get) {
            outPut << get_method_modifier(il2cpp_method_get_flags(get, &iflags));
            prop_type = il2cpp_method_get_return_type(get);
        } else if (set) {
            outPut << get_method_modifier(il2cpp_method_get_flags(set, &iflags));
            prop_type = il2cpp_method_get_param(set, 0);
        }
        if (prop_type) {
            std::string p_cls_name = get_type_name(prop_type);
            outPut << p_cls_name << " " << (prop_name ? prop_name : "unknown_prop") << " { ";
            if (get) {
                outPut << "get; ";
            }
            if (set) {
                outPut << "set; ";
            }
            outPut << "}\n";
        } else {
            if (prop_name) {
                outPut << " // unknown property " << prop_name << "\n";
            }
        }
    }
    return outPut.str();
}

std::string dump_field(Il2CppClass *klass) {
    if (!klass) return "";
    std::stringstream outPut;
    outPut << "\n\t// Fields\n";
    auto is_enum = il2cpp_class_is_enum(klass);
    void *iter = nullptr;
    while (auto field = il2cpp_class_get_fields(klass, &iter)) {
        outPut << "\t";
        auto attrs = il2cpp_field_get_flags(field);
        auto access = attrs & FIELD_ATTRIBUTE_FIELD_ACCESS_MASK;
        switch (access) {
            case FIELD_ATTRIBUTE_PRIVATE:
                outPut << "private ";
                break;
            case FIELD_ATTRIBUTE_PUBLIC:
                outPut << "public ";
                break;
            case FIELD_ATTRIBUTE_FAMILY:
                outPut << "protected ";
                break;
            case FIELD_ATTRIBUTE_ASSEMBLY:
            case FIELD_ATTRIBUTE_FAM_AND_ASSEM:
                outPut << "internal ";
                break;
            case FIELD_ATTRIBUTE_FAM_OR_ASSEM:
                outPut << "protected internal ";
                break;
        }
        if (attrs & FIELD_ATTRIBUTE_LITERAL) {
            outPut << "const ";
        } else {
            if (attrs & FIELD_ATTRIBUTE_STATIC) {
                outPut << "static ";
            }
            if (attrs & FIELD_ATTRIBUTE_INIT_ONLY) {
                outPut << "readonly ";
            }
        }
        auto field_type = il2cpp_field_get_type(field);
        if (!field_type) continue;
        std::string fld_cls_name = get_type_name(field_type);
        const char *fld_name = il2cpp_field_get_name(field);
        outPut << fld_cls_name << " " << (fld_name ? fld_name : "unknown_field");

        // Dump constant literals
        if (attrs & FIELD_ATTRIBUTE_LITERAL) {
            if (is_enum) {
                auto base_type = il2cpp_class_enum_basetype ? il2cpp_class_enum_basetype(klass) : nullptr;
                int base_type_idx = base_type ? base_type->type : IL2CPP_TYPE_I4;
                switch (base_type_idx) {
                    case IL2CPP_TYPE_I1: {
                        int8_t v = 0;
                        il2cpp_field_static_get_value(field, &v);
                        outPut << " = " << (int)v;
                        break;
                    }
                    case IL2CPP_TYPE_U1: {
                        uint8_t v = 0;
                        il2cpp_field_static_get_value(field, &v);
                        outPut << " = " << (unsigned int)v;
                        break;
                    }
                    case IL2CPP_TYPE_I2: {
                        int16_t v = 0;
                        il2cpp_field_static_get_value(field, &v);
                        outPut << " = " << v;
                        break;
                    }
                    case IL2CPP_TYPE_U2: {
                        uint16_t v = 0;
                        il2cpp_field_static_get_value(field, &v);
                        outPut << " = " << v;
                        break;
                    }
                    case IL2CPP_TYPE_I4: {
                        int32_t v = 0;
                        il2cpp_field_static_get_value(field, &v);
                        outPut << " = " << v;
                        break;
                    }
                    case IL2CPP_TYPE_U4: {
                        uint32_t v = 0;
                        il2cpp_field_static_get_value(field, &v);
                        outPut << " = " << v;
                        break;
                    }
                    case IL2CPP_TYPE_I8: {
                        int64_t v = 0;
                        il2cpp_field_static_get_value(field, &v);
                        outPut << " = " << v << "L";
                        break;
                    }
                    case IL2CPP_TYPE_U8: {
                        uint64_t v = 0;
                        il2cpp_field_static_get_value(field, &v);
                        outPut << " = " << v << "UL";
                        break;
                    }
                    default: {
                        uint64_t v = 0;
                        il2cpp_field_static_get_value(field, &v);
                        outPut << " = " << v;
                        break;
                    }
                }
            } else if (il2cpp_field_static_get_value) {
                switch (field_type->type) {
                    case IL2CPP_TYPE_BOOLEAN: {
                        bool b = false;
                        il2cpp_field_static_get_value(field, &b);
                        outPut << " = " << (b ? "true" : "false");
                        break;
                    }
                    case IL2CPP_TYPE_I1: {
                        int8_t v = 0;
                        il2cpp_field_static_get_value(field, &v);
                        outPut << " = " << (int)v;
                        break;
                    }
                    case IL2CPP_TYPE_U1: {
                        uint8_t v = 0;
                        il2cpp_field_static_get_value(field, &v);
                        outPut << " = " << (int)v;
                        break;
                    }
                    case IL2CPP_TYPE_I2: {
                        int16_t v = 0;
                        il2cpp_field_static_get_value(field, &v);
                        outPut << " = " << v;
                        break;
                    }
                    case IL2CPP_TYPE_U2:
                    case IL2CPP_TYPE_CHAR: {
                        uint16_t v = 0;
                        il2cpp_field_static_get_value(field, &v);
                        outPut << " = " << v;
                        break;
                    }
                    case IL2CPP_TYPE_I4: {
                        int32_t v = 0;
                        il2cpp_field_static_get_value(field, &v);
                        outPut << " = " << v;
                        break;
                    }
                    case IL2CPP_TYPE_U4: {
                        uint32_t v = 0;
                        il2cpp_field_static_get_value(field, &v);
                        outPut << " = " << v;
                        break;
                    }
                    case IL2CPP_TYPE_I8: {
                        int64_t v = 0;
                        il2cpp_field_static_get_value(field, &v);
                        outPut << " = " << v << "L";
                        break;
                    }
                    case IL2CPP_TYPE_U8: {
                        uint64_t v = 0;
                        il2cpp_field_static_get_value(field, &v);
                        outPut << " = " << v << "UL";
                        break;
                    }
                    case IL2CPP_TYPE_R4: {
                        float v = 0.0f;
                        il2cpp_field_static_get_value(field, &v);
                        outPut << " = " << v << "f";
                        break;
                    }
                    case IL2CPP_TYPE_R8: {
                        double v = 0.0;
                        il2cpp_field_static_get_value(field, &v);
                        outPut << " = " << v;
                        break;
                    }
                    case IL2CPP_TYPE_STRING: {
                        Il2CppString *str = nullptr;
                        il2cpp_field_static_get_value(field, &str);
                        if (str && il2cpp_string_chars && il2cpp_string_length) {
                            auto chars = il2cpp_string_chars(str);
                            auto len = il2cpp_string_length(str);
                            std::string s;
                            for (int k = 0; k < len; ++k) {
                                if (chars[k] == '\"') s += "\\\"";
                                else if (chars[k] == '\\') s += "\\\\";
                                else if (chars[k] == '\n') s += "\\n";
                                else if (chars[k] == '\r') s += "\\r";
                                else if (chars[k] == '\t') s += "\\t";
                                else if (chars[k] >= 32 && chars[k] <= 126) s += (char)chars[k];
                                else s += '?';
                            }
                            outPut << " = \"" << s << "\"";
                        }
                        break;
                    }
                    default:
                        break;
                }
            }
        } else if (il2cpp_field_static_get_value && (attrs & FIELD_ATTRIBUTE_STATIC)) {
            if (field_type->type == IL2CPP_TYPE_STRING) {
                Il2CppString *str = nullptr;
                il2cpp_field_static_get_value(field, &str);
                if (str && il2cpp_string_chars && il2cpp_string_length) {
                    auto chars = il2cpp_string_chars(str);
                    auto len = il2cpp_string_length(str);
                    if (len > 0 && len < 2048) {
                        std::string s;
                        for (int k = 0; k < len; ++k) {
                            if (chars[k] == '\"') s += "\\\"";
                            else if (chars[k] == '\\') s += "\\\\";
                            else if (chars[k] == '\n') s += "\\n";
                            else if (chars[k] == '\r') s += "\\r";
                            else if (chars[k] == '\t') s += "\\t";
                            else if (chars[k] >= 32 && chars[k] <= 126) s += (char)chars[k];
                            else s += '?';
                        }
                        outPut << " /* runtime value = \"" << s << "\" */";
                    }
                }
            }
        }

        outPut << "; // 0x" << std::hex << il2cpp_field_get_offset(field) << "\n";
    }
    return outPut.str();
}

std::string dump_type(Il2CppClass *klass, const char *image_name) {
    if (!klass) return "";
    std::stringstream outPut;
    if (image_name) {
        outPut << "\n// Dll : " << image_name << "\n";
    }
    const char *ns = il2cpp_class_get_namespace(klass);
    outPut << "// Namespace: " << (ns ? ns : "") << "\n";
    auto flags = il2cpp_class_get_flags(klass);
    if (flags & TYPE_ATTRIBUTE_SERIALIZABLE) {
        outPut << "[Serializable]\n";
    }
    auto is_valuetype = il2cpp_class_is_valuetype(klass);
    auto is_enum = il2cpp_class_is_enum(klass);
    auto visibility = flags & TYPE_ATTRIBUTE_VISIBILITY_MASK;
    switch (visibility) {
        case TYPE_ATTRIBUTE_PUBLIC:
        case TYPE_ATTRIBUTE_NESTED_PUBLIC:
            outPut << "public ";
            break;
        case TYPE_ATTRIBUTE_NOT_PUBLIC:
        case TYPE_ATTRIBUTE_NESTED_FAM_AND_ASSEM:
        case TYPE_ATTRIBUTE_NESTED_ASSEMBLY:
            outPut << "internal ";
            break;
        case TYPE_ATTRIBUTE_NESTED_PRIVATE:
            outPut << "private ";
            break;
        case TYPE_ATTRIBUTE_NESTED_FAMILY:
            outPut << "protected ";
            break;
        case TYPE_ATTRIBUTE_NESTED_FAM_OR_ASSEM:
            outPut << "protected internal ";
            break;
    }
    if (flags & TYPE_ATTRIBUTE_ABSTRACT && flags & TYPE_ATTRIBUTE_SEALED) {
        outPut << "static ";
    } else if (!(flags & TYPE_ATTRIBUTE_INTERFACE) && flags & TYPE_ATTRIBUTE_ABSTRACT) {
        outPut << "abstract ";
    } else if (!is_valuetype && !is_enum && flags & TYPE_ATTRIBUTE_SEALED) {
        outPut << "sealed ";
    }
    if (flags & TYPE_ATTRIBUTE_INTERFACE) {
        outPut << "interface ";
    } else if (is_enum) {
        outPut << "enum ";
    } else if (is_valuetype) {
        outPut << "struct ";
    } else {
        outPut << "class ";
    }
    const char *cls_name = il2cpp_class_get_name(klass);
    outPut << (cls_name ? cls_name : "unknown_class");
    
    std::vector<std::string> extends;
    auto parent = il2cpp_class_get_parent(klass);
    if (!is_valuetype && !is_enum && parent) {
        auto parent_type = il2cpp_class_get_type(parent);
        if (parent_type && parent_type->type != IL2CPP_TYPE_OBJECT) {
            const char *p_name = il2cpp_class_get_name(parent);
            if (p_name) extends.emplace_back(p_name);
        }
    }
    void *iter = nullptr;
    while (auto itf = il2cpp_class_get_interfaces(klass, &iter)) {
        const char *itf_name = il2cpp_class_get_name(itf);
        if (itf_name) extends.emplace_back(itf_name);
    }
    if (!extends.empty()) {
        outPut << " : " << extends[0];
        for (size_t i = 1; i < extends.size(); ++i) {
            outPut << ", " << extends[i];
        }
    }
    outPut << "\n{";
    outPut << dump_field(klass);
    outPut << dump_property(klass);
    outPut << dump_method(klass);
    outPut << "}\n";
    return outPut.str();
}

static void dump_class_with_nested(
    Il2CppClass *klass,
    const char *image_name,
    std::ofstream &outStream,
    std::ofstream &scriptStream,
    bool scriptOpen,
    bool &firstMethod,
    std::unordered_set<const Il2CppClass *> &visitedClasses
) {
    if (!klass || visitedClasses.count(klass)) return;
    visitedClasses.insert(klass);

    outStream << dump_type(klass, image_name);

    if (scriptOpen) {
        void *mIter = nullptr;
        const char *ns = il2cpp_class_get_namespace(klass);
        const char *cls = il2cpp_class_get_name(klass);
        std::string fullCls = (ns && strlen(ns) > 0) ? (std::string(ns) + "." + (cls ? cls : "unknown")) : (cls ? cls : "unknown");

        while (auto method = il2cpp_class_get_methods(klass, &mIter)) {
            if (method->methodPointer) {
                uint64_t rva = (uint64_t) method->methodPointer - il2cpp_base;
                const char *mName = il2cpp_method_get_name(method);
                if (!firstMethod) scriptStream << ",\n";
                firstMethod = false;
                scriptStream << "    {\"Address\": " << rva << ", \"Name\": \""
                             << json_escape(fullCls.c_str()) << "$$"
                             << json_escape(mName ? mName : "unknown") << "\"}";
            }
        }
    }

    if (il2cpp_class_get_nested_types) {
        void *nIter = nullptr;
        while (auto nested = il2cpp_class_get_nested_types(klass, &nIter)) {
            dump_class_with_nested(nested, image_name, outStream, scriptStream, scriptOpen, firstMethod, visitedClasses);
        }
    }
}

static const Il2CppGlobalMetadataHeader *find_metadata_header() {
    FILE *maps = fopen("/proc/self/maps", "r");
    if (!maps) return nullptr;
    char line[512];
    const Il2CppGlobalMetadataHeader *found = nullptr;
    while (fgets(line, sizeof(line), maps)) {
        uintptr_t start = 0, end = 0;
        char perms[5] = {0};
        if (sscanf(line, "%" SCNxPTR "-%" SCNxPTR " %4s", &start, &end, perms) >= 3) {
            if (perms[0] == 'r' && end > start && (end - start) >= sizeof(Il2CppGlobalMetadataHeader)) {
                auto hdr = reinterpret_cast<const Il2CppGlobalMetadataHeader *>(start);
                if (hdr->magic == (int32_t)0xFAB11BAF) {
                    if (hdr->version >= 16 && hdr->version <= 39 && hdr->stringLiteralCount > 0) {
                        found = hdr;
                        break;
                    }
                }
            }
        }
    }
    fclose(maps);
    return found;
}

static void dump_global_metadata_strings(const std::string &outPath, std::ofstream &scriptStream, bool scriptOpen) {
    auto header = find_metadata_header();
    if (!header) {
        write_log("[-] Decrypted global-metadata.dat not found in memory maps");
        return;
    }

    write_log("[+] Found global-metadata in RAM (magic: 0x%x, version: %d, stringLiteralCount: %d)",
              header->magic, header->version, header->stringLiteralCount);

    const char *meta = reinterpret_cast<const char *>(header);
    auto entries = reinterpret_cast<const Il2CppStringLiteralEntry *>(meta + header->stringLiteralOffset);
    const char *stringData = meta + header->stringLiteralDataOffset;

    size_t count = header->stringLiteralCount / sizeof(Il2CppStringLiteralEntry);
    write_log("[+] Extracting %zu string literals...", count);

    std::ofstream strFile(outPath, std::ios::out | std::ios::trunc);
    if (strFile.is_open()) {
        strFile << "[\n";
    }

    bool firstStr = true;
    for (size_t i = 0; i < count; ++i) {
        uint32_t length = entries[i].length;
        int32_t dataIndex = entries[i].dataIndex;
        if (dataIndex >= 0 && (dataIndex + length) <= (uint32_t)header->stringLiteralDataCount) {
            const char *str = stringData + dataIndex;
            std::string escaped = json_escape(str, length);

            if (strFile.is_open()) {
                if (!firstStr) strFile << ",\n";
                strFile << "  {\"Index\": " << i << ", \"Value\": \"" << escaped << "\"}";
            }

            if (scriptOpen) {
                if (!firstStr) scriptStream << ",\n";
                scriptStream << "    {\"Address\": " << i << ", \"Value\": \"" << escaped << "\"}";
            }

            firstStr = false;
        }
    }

    if (strFile.is_open()) {
        strFile << "\n]\n";
        strFile.close();
        write_log("[+] All string literals exported to %s", outPath.c_str());
    }
}

void il2cpp_api_init(void *handle) {
    write_log("[+] Initializing IL2CPP API functions from %p...", handle);
    init_il2cpp_api(handle);

    if (il2cpp_domain_get_assemblies) {
        Dl_info dlInfo;
        if (dladdr((void *) il2cpp_domain_get_assemblies, &dlInfo)) {
            il2cpp_base = reinterpret_cast<uint64_t>(dlInfo.dli_fbase);
        }
        write_log("[+] il2cpp_base: 0x%" PRIx64, il2cpp_base);
    } else {
        std::string err = "Failed to initialize il2cpp APIs (missing symbols)";
        write_log("[-] %s", err.c_str());
        show_toast(std::string("[Il2CppDumper] FAIL: ") + err);
        return;
    }

    Il2CppDomain *domain = nullptr;
    int retry = 0;
    while (retry < 60) {
        if (il2cpp_domain_get && il2cpp_domain_get_assemblies) {
            domain = il2cpp_domain_get();
            if (domain) {
                size_t asmsCount = 0;
                auto asms = il2cpp_domain_get_assemblies(domain, &asmsCount);
                if (asms && asmsCount > 0) {
                    write_log("[+] il2cpp domain and assemblies ready: %p (%zu assemblies)", domain, asmsCount);
                    break;
                }
            }
        }
        sleep(2);
        retry++;
    }

    if (!domain) {
        std::string err = "Timed out waiting for il2cpp domain & assemblies (120s)";
        write_log("[-] %s", err.c_str());
        show_toast(std::string("[Il2CppDumper] FAIL: ") + err);
    }
}

void il2cpp_dump(const char *outDir) {
    write_log("[+] Starting IL2CPP dump for %s...", outDir);
    show_toast("[Il2CppDumper] Dumping in progress, please wait...");

    if (!il2cpp_domain_get || !il2cpp_domain_get_assemblies) {
        std::string err = "Required il2cpp APIs are missing";
        write_log("[-] %s", err.c_str());
        show_toast(std::string("[Il2CppDumper] FAIL: ") + err);
        return;
    }

    auto domain = il2cpp_domain_get();
    if (!domain) {
        std::string err = "il2cpp domain is null, cannot dump";
        write_log("[-] %s", err.c_str());
        show_toast(std::string("[Il2CppDumper] FAIL: ") + err);
        return;
    }

    if (il2cpp_thread_attach) {
        s_attached_thread = il2cpp_thread_attach(domain);
        write_log("[+] Native thread attached to IL2CPP domain: %p", s_attached_thread);
    }

    size_t size = 0;
    auto assemblies = il2cpp_domain_get_assemblies(domain, &size);
    if (!assemblies || size == 0) {
        std::string err = "No assemblies found in domain";
        write_log("[-] %s", err.c_str());
        show_toast(std::string("[Il2CppDumper] FAIL: ") + err);
        return;
    }

    std::string boDir = "/sdcard/Bo_dump";
    mkdir(boDir.c_str(), 0777);
    auto outPath = boDir + "/dump.cs";
    auto scriptPath = boDir + "/script.json";
    auto strLiteralPath = boDir + "/stringliteral.json";

    write_log("[+] Found %zu assemblies. Opening streaming output files in %s...", size, boDir.c_str());

    LOGI("Streaming dump output directly to %s", outPath.c_str());
    std::ofstream outStream(outPath, std::ios::out | std::ios::trunc);
    if (!outStream.is_open()) {
        LOGE("Failed to open %s for writing!", outPath.c_str());
        return;
    }

    std::ofstream scriptStream(scriptPath, std::ios::out | std::ios::trunc);
    bool scriptOpen = scriptStream.is_open();
    bool firstMethod = true;
    if (scriptOpen) {
        scriptStream << "{\n  \"ScriptMethod\": [\n";
    }

    for (size_t i = 0; i < size; ++i) {
        auto image = il2cpp_assembly_get_image(assemblies[i]);
        if (image) {
            auto image_name = il2cpp_image_get_name(image);
            outStream << "// Image " << i << ": " << (image_name ? image_name : "unknown") << "\n";
        }
    }
    outStream.flush();

    std::unordered_set<const Il2CppClass *> visitedClasses;

    if (il2cpp_image_get_class) {
        LOGI("Dumping all assemblies and nested classes (total %zu assemblies)...", size);
        for (size_t i = 0; i < size; ++i) {
            auto image = il2cpp_assembly_get_image(assemblies[i]);
            if (!image) continue;
            auto image_name = il2cpp_image_get_name(image);

            auto classCount = il2cpp_image_get_class_count(image);
            for (size_t j = 0; j < classCount; ++j) {
                auto klass = il2cpp_image_get_class(image, j);
                if (!klass) continue;
                dump_class_with_nested(
                    const_cast<Il2CppClass *>(klass),
                    image_name,
                    outStream,
                    scriptStream,
                    scriptOpen,
                    firstMethod,
                    visitedClasses
                );
            }
            outStream.flush();
            if (scriptOpen) scriptStream.flush();
        }
    }

    outStream.close();
    write_log("[+] dump.cs generated successfully (%zu unique classes dumped) at %s", visitedClasses.size(), outPath.c_str());

    if (scriptOpen) {
        scriptStream << "\n  ],\n  \"ScriptString\": [\n";
        dump_global_metadata_strings(strLiteralPath, scriptStream, scriptOpen);
        scriptStream << "\n  ],\n  \"ScriptMetadata\": [],\n  \"ScriptMetadataMethod\": []\n}\n";
        scriptStream.close();
        write_log("[+] script.json generated successfully at %s", scriptPath.c_str());
    }

    write_log("[+] ALL DUMP OPERATIONS COMPLETED SUCCESSFULLY!");
    show_toast("[Il2CppDumper] SUCCESS! Saved to /sdcard/Bo_dump/");
}
```

---

## 8. `module/src/main/cpp/CMakeLists.txt`
```cmake
cmake_minimum_required(VERSION 3.18.1)

if (NOT DEFINED MODULE_NAME)
    message(FATAL_ERROR "MODULE_NAME is not set")
else ()
    project(${MODULE_NAME})
endif ()

message("Build type: ${CMAKE_BUILD_TYPE}")

set(CMAKE_CXX_STANDARD 20)

set(LINKER_FLAGS "-ffixed-x18 -Wl,--hash-style=both")
set(C_FLAGS "-Werror=format -fdata-sections -ffunction-sections")
set(CXX_FLAGS "${CXX_FLAGS} -fno-exceptions -fno-rtti")

if (NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(C_FLAGS "${C_FLAGS} -O3 -fvisibility=hidden -fvisibility-inlines-hidden")
    set(LINKER_FLAGS "${LINKER_FLAGS} -Wl,-exclude-libs,ALL -Wl,--gc-sections -Wl,--strip-all")
else ()
    set(C_FLAGS "${C_FLAGS} -O0")
endif ()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${C_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${C_FLAGS} ${CXX_FLAGS}")

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${LINKER_FLAGS}")
set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} ${LINKER_FLAGS}")

include_directories(
        xdl/include
)

aux_source_directory(xdl xdl-src)

add_library(${MODULE_NAME} SHARED
        main.cpp
        hack.cpp
        il2cpp_dump.cpp
        ${xdl-src})
target_link_libraries(${MODULE_NAME} log)

if (NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_custom_command(TARGET ${MODULE_NAME} POST_BUILD
            COMMAND ${CMAKE_STRIP} --strip-all --remove-section=.comment "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/lib${MODULE_NAME}.so")
endif ()
```

---

## 9. `module.gradle`
```groovy
ext {
    moduleLibraryName = "il2cppdumper"
    magiskModuleId = "zygisk_il2cppdumper"
    moduleName = "Il2CppDumper"
    moduleAuthor = "Perfare & Miyu"
    moduleDescription = "Modern runtime IL2CPP Dumper with streaming write, dynamic target switching and WebUI."
    moduleVersion = "v2.1.0"
    moduleVersionCode = 210
}
```

---

## 10. `template/magisk_module/module.prop`
```properties
id=zygisk_il2cppdumper
name=Il2CppDumper
version=v2.1.0
versionCode=210
author=Perfare & Miyu
description=Modern runtime IL2CPP Dumper with streaming write, dynamic target switching and WebUI.
```

---

## 11. `template/magisk_module/action.sh`
```bash
#!/system/bin/sh
# KernelSU / APatch / Magisk Action Script for Zygisk-Il2CppDumper

echo "=========================================="
echo "        Zygisk-Il2CppDumper Manager       "
echo "=========================================="

DUMP_DIR="/sdcard/Bo_dump"
echo "Dump Directory: $DUMP_DIR"
echo ""

if [ -d "$DUMP_DIR" ]; then
    echo "[+] Files in $DUMP_DIR:"
    ls -lh "$DUMP_DIR" 2>/dev/null
else
    echo "[-] Directory $DUMP_DIR not created yet."
fi

echo ""
PROP_PKG=$(getprop persist.il2cppdumper.package)
if [ -n "$PROP_PKG" ]; then
    echo "Target Package (override): $PROP_PKG"
else
    echo "Target Package: com.dts.freefireth (default)"
fi

if [ -f "/data/local/tmp/il2cppdumper_disabled" ]; then
    echo "Dumper Engine Status: [ DISABLED ]"
else
    echo "Dumper Engine Status: [ ENABLED ]"
fi

echo "=========================================="
```

---

## 12. `template/magisk_module/webroot/index.html`
```html
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
  <title>Zygisk-Il2CppDumper</title>
  <style>
    :root {
      --bg: #090d16;
      --card-bg: rgba(21, 28, 44, 0.75);
      --card-border: rgba(0, 230, 118, 0.2);
      --text: #e6edf3;
      --text-muted: #8b949e;
      --accent: #00e676;
      --accent-glow: rgba(0, 230, 118, 0.35);
      --cyan: #00e5ff;
      --danger: #ff1744;
      --danger-glow: rgba(255, 23, 68, 0.35);
    }
    * { box-sizing: border-box; -webkit-tap-highlight-color: transparent; }
    body {
      margin: 0;
      padding: 16px;
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", sans-serif;
      background: var(--bg);
      background-image: radial-gradient(circle at top right, rgba(0,229,255,0.08), transparent 40%),
                        radial-gradient(circle at bottom left, rgba(0,230,118,0.08), transparent 40%);
      color: var(--text);
      min-height: 100vh;
    }
    .header {
      display: flex;
      align-items: center;
      justify-content: space-between;
      margin-bottom: 14px;
      padding-bottom: 12px;
      border-bottom: 1px solid rgba(255, 255, 255, 0.08);
    }
    .title-group h1 {
      margin: 0;
      font-size: 20px;
      font-weight: 800;
      letter-spacing: -0.5px;
      background: linear-gradient(135deg, #00e676, #00e5ff);
      -webkit-background-clip: text;
      -webkit-text-fill-color: transparent;
    }
    .title-group p {
      margin: 3px 0 0;
      font-size: 11px;
      color: var(--text-muted);
      letter-spacing: 0.5px;
      text-transform: uppercase;
    }
    .badge {
      font-size: 11px;
      padding: 4px 10px;
      border-radius: 20px;
      font-weight: 700;
      letter-spacing: 0.5px;
      display: inline-flex;
      align-items: center;
      gap: 5px;
    }
    .badge-on { background: rgba(0, 230, 118, 0.15); color: var(--accent); border: 1px solid var(--accent); box-shadow: 0 0 10px var(--accent-glow); }
    .badge-off { background: rgba(255, 23, 68, 0.15); color: var(--danger); border: 1px solid var(--danger); box-shadow: 0 0 10px var(--danger-glow); }
    .badge-dot { width: 6px; height: 6px; border-radius: 50%; background: currentColor; }
    
    .card {
      background: var(--card-bg);
      border: 1px solid var(--card-border);
      border-radius: 14px;
      padding: 16px;
      margin-bottom: 14px;
      backdrop-filter: blur(12px);
      box-shadow: 0 8px 24px rgba(0, 0, 0, 0.4);
    }
    .card-title {
      font-size: 13px;
      font-weight: 700;
      color: var(--cyan);
      text-transform: uppercase;
      letter-spacing: 1px;
      margin-bottom: 12px;
      display: flex;
      align-items: center;
      justify-content: space-between;
    }
    .switch-row {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 12px;
    }
    .switch-label {
      font-size: 14px;
      font-weight: 600;
    }
    .switch-desc {
      font-size: 11px;
      color: var(--text-muted);
      margin-top: 2px;
    }
    
    .toggle-btn {
      padding: 10px 18px;
      border-radius: 10px;
      border: none;
      font-weight: 700;
      font-size: 13px;
      cursor: pointer;
      transition: all 0.2s ease;
      display: inline-flex;
      align-items: center;
      gap: 6px;
    }
    .toggle-active {
      background: linear-gradient(135deg, #00e676, #00b0ff);
      color: #05140b;
      box-shadow: 0 0 16px var(--accent-glow);
    }
    .toggle-inactive {
      background: #21262d;
      color: var(--text-muted);
      border: 1px solid rgba(255, 255, 255, 0.1);
    }

    input[type="text"] {
      width: 100%;
      background: #0d1117;
      border: 1px solid rgba(255, 255, 255, 0.15);
      border-radius: 10px;
      padding: 12px 14px;
      font-size: 14px;
      color: #fff;
      outline: none;
      transition: border 0.2s;
    }
    input[type="text"]:focus {
      border-color: var(--cyan);
      box-shadow: 0 0 8px rgba(0, 229, 255, 0.3);
    }
    .presets {
      display: flex;
      gap: 8px;
      margin-top: 10px;
      flex-wrap: wrap;
    }
    .chip {
      background: #161b22;
      border: 1px solid rgba(255, 255, 255, 0.12);
      border-radius: 8px;
      padding: 6px 12px;
      font-size: 11px;
      color: #c9d1d9;
      cursor: pointer;
      transition: background 0.2s;
    }
    .chip:active { background: #21262d; }

    .btn {
      width: 100%;
      padding: 13px;
      border-radius: 10px;
      border: none;
      font-size: 14px;
      font-weight: 700;
      cursor: pointer;
      display: flex;
      align-items: center;
      justify-content: center;
      gap: 8px;
      transition: transform 0.1s, filter 0.2s;
      margin-top: 10px;
    }
    .btn:active { transform: scale(0.98); }
    .btn-primary {
      background: linear-gradient(135deg, #00e676, #00c853);
      color: #03200e;
      box-shadow: 0 4px 14px var(--accent-glow);
    }
    .btn-cyan {
      background: linear-gradient(135deg, #00e5ff, #0091ea);
      color: #021a2f;
      box-shadow: 0 4px 14px rgba(0, 229, 255, 0.3);
    }
    .btn-danger {
      background: #21262d;
      color: var(--danger);
      border: 1px solid rgba(255, 23, 68, 0.4);
    }
    .btn-danger:active { background: rgba(255, 23, 68, 0.1); }

    .path-badge {
      font-family: monospace;
      font-size: 12px;
      color: #ffd600;
      background: rgba(255, 214, 0, 0.1);
      padding: 4px 8px;
      border-radius: 6px;
      border: 1px solid rgba(255, 214, 0, 0.3);
    }

    .terminal {
      background: #05080f;
      border: 1px solid rgba(255, 255, 255, 0.1);
      border-radius: 10px;
      padding: 12px;
      font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace;
      font-size: 12px;
      color: #58a6ff;
      white-space: pre-wrap;
      word-break: break-all;
      max-height: 220px;
      overflow-y: auto;
      margin-top: 12px;
    }
  </style>
</head>
<body>
  <div class="header">
    <div class="title-group">
      <h1>Zygisk-Il2CppDumper</h1>
      <p>Universal In-Memory Engine v2.1.0</p>
    </div>
    <div id="statusBadge" class="badge badge-on">
      <span class="badge-dot"></span>
      <span id="statusBadgeText">ACTIVE</span>
    </div>
  </div>

  <!-- Master Switch Card -->
  <div class="card">
    <div class="switch-row">
      <div>
        <div class="switch-label">Master Dumper Switch</div>
        <div class="switch-desc">Turn off to play cleanly without uninstalling module</div>
      </div>
      <button id="toggleMasterBtn" class="toggle-btn toggle-active" onclick="toggleMasterSwitch()">
        <span id="toggleMasterText">ON</span>
      </button>
    </div>
  </div>

  <!-- Target Package Card -->
  <div class="card">
    <div class="card-title">
      <span>Target Package</span>
      <span class="path-badge">AUTO-DETECT</span>
    </div>
    <input type="text" id="pkgInput" value="com.dts.freefireth" placeholder="Package name (e.g. com.dts.freefireth)" />
    
    <div class="presets">
      <div class="chip" onclick="setPreset('com.dts.freefireth')">Free Fire TH</div>
      <div class="chip" onclick="setPreset('com.dts.freefiremax')">Free Fire MAX</div>
      <div class="chip" onclick="setPreset('com.dts.freefire')">Free Fire VN</div>
    </div>
    <button class="btn btn-primary" onclick="savePackage()">Apply Target Package</button>
  </div>

  <!-- Storage and Dump Files Card -->
  <div class="card">
    <div class="card-title">
      <span>Output Destination</span>
      <span class="path-badge">/sdcard/Bo_dump/</span>
    </div>
    <button class="btn btn-cyan" onclick="checkDumpFiles()">Scan Dump Files in /sdcard/Bo_dump</button>
    <button class="btn btn-danger" onclick="cleanDumpDir()">Delete Old Dumps in /sdcard/Bo_dump</button>
    
    <div id="output" class="terminal">[Ready] Dumper engine primed. Output destination: /sdcard/Bo_dump/</div>
  </div>

  <script>
    let isEnabled = true;

    function printLog(msg) {
      document.getElementById('output').innerText = msg;
    }

    function exec(cmd) {
      return new Promise((resolve) => {
        try {
          if (typeof ksu !== 'undefined' && ksu && ksu.exec) {
            const result = ksu.exec(cmd);
            if (result && typeof result.then === 'function') {
              result.then((out) => {
                if (typeof out === 'string') {
                  resolve({ errno: 0, stdout: out, stderr: '' });
                } else {
                  resolve(out || { errno: 0, stdout: '', stderr: '' });
                }
              }).catch((err) => {
                resolve({ errno: -1, stdout: '', stderr: String(err) });
              });
              return;
            }
            ksu.exec(cmd, (errno, stdout, stderr) => {
              resolve({ errno, stdout, stderr });
            });
            return;
          }
          resolve({ errno: -1, stdout: '', stderr: 'KernelSU/APatch API not found (running outside root manager)' });
        } catch (e) {
          resolve({ errno: -1, stdout: '', stderr: String(e) });
        }
      });
    }

    async function init() {
      const chkDisabled = await exec('sh -c "if [ -f /data/local/tmp/il2cppdumper_disabled ] || [ \\\"$(getprop persist.il2cppdumper.enabled)\\\" = \\\"0\\\" ]; then echo 0; else echo 1; fi"');
      if (chkDisabled.stdout && chkDisabled.stdout.trim() === '0') {
        setSwitchUI(false);
      } else {
        setSwitchUI(true);
      }

      const resPkg = await exec('getprop persist.il2cppdumper.package');
      if (resPkg.stdout && resPkg.stdout.trim()) {
        document.getElementById('pkgInput').value = resPkg.stdout.trim();
        printLog('Target Package: ' + resPkg.stdout.trim() + '\nOutput Path: /sdcard/Bo_dump/\nEngine: ' + (isEnabled ? 'ACTIVE' : 'DISABLED'));
      } else {
        printLog('Target Package: com.dts.freefireth (default)\nOutput Path: /sdcard/Bo_dump/\nEngine: ' + (isEnabled ? 'ACTIVE' : 'DISABLED'));
      }
    }

    function setSwitchUI(enabled) {
      isEnabled = enabled;
      const badge = document.getElementById('statusBadge');
      const badgeText = document.getElementById('statusBadgeText');
      const toggleBtn = document.getElementById('toggleMasterBtn');
      const toggleText = document.getElementById('toggleMasterText');

      if (enabled) {
        badge.className = 'badge badge-on';
        badgeText.innerText = 'ACTIVE';
        toggleBtn.className = 'toggle-btn toggle-active';
        toggleText.innerText = 'ON';
      } else {
        badge.className = 'badge badge-off';
        badgeText.innerText = 'DISABLED';
        toggleBtn.className = 'toggle-btn toggle-inactive';
        toggleText.innerText = 'OFF';
      }
    }

    async function toggleMasterSwitch() {
      if (isEnabled) {
        await exec('sh -c "touch /data/local/tmp/il2cppdumper_disabled && setprop persist.il2cppdumper.enabled 0"');
        setSwitchUI(false);
        printLog('[Dumper Switch] DISABLED. Games will run 100% untouched normally without dumping or hooking.');
      } else {
        await exec('sh -c "rm -f /data/local/tmp/il2cppdumper_disabled && setprop persist.il2cppdumper.enabled 1"');
        setSwitchUI(true);
        printLog('[Dumper Switch] ENABLED. Dumper will attach to target package upon launch.');
      }
    }

    function setPreset(pkg) {
      document.getElementById('pkgInput').value = pkg;
    }

    async function savePackage() {
      const pkg = document.getElementById('pkgInput').value.trim();
      if (!pkg) return;
      await exec('setprop persist.il2cppdumper.package ' + pkg);
      printLog('[Config Saved] Target game package set to: ' + pkg);
    }

    async function checkDumpFiles() {
      printLog('Scanning /sdcard/Bo_dump/ ...');
      const cmd = 'sh -c "if [ -d /sdcard/Bo_dump ]; then ls -lh /sdcard/Bo_dump; else echo [-] /sdcard/Bo_dump directory does not exist yet. Launch the game to dump.; fi"';
      const res = await exec(cmd);
      if (res.stdout && res.stdout.trim()) {
        printLog('[Files in /sdcard/Bo_dump/]\n' + res.stdout.trim());
      } else {
        printLog(res.stderr || 'No files found in /sdcard/Bo_dump/. Launch the game to dump.');
      }
    }

    async function cleanDumpDir() {
      printLog('Cleaning /sdcard/Bo_dump/ ...');
      const cmd = 'sh -c "rm -rf /sdcard/Bo_dump/* && echo [SUCCESS] Cleared all files in /sdcard/Bo_dump/"';
      const res = await exec(cmd);
      printLog(res.stdout || res.stderr);
    }

    init();
  </script>
</body>
</html>
```
