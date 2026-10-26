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
            std::thread hack_thread(hack_prepare, pkg_name, game_data_dir, data, length);
            hack_thread.detach();
        }
    }

private:
    Api *api;
    JNIEnv *env;
    bool enable_hack;
    char *pkg_name;
    char *game_data_dir;
    void *data;
    size_t length;

    bool is_dumper_enabled() {
        char prop[PROP_VALUE_MAX] = {0};
        __system_property_get("persist.il2cppdumper.enabled", prop);
        if (strlen(prop) > 0 && strcmp(prop, "0") == 0) {
            return false;
        }
        return true;
    }

    void preSpecialize(const char *package_name, const char *app_data_dir) {
        if (!is_dumper_enabled()) {
            LOGI("Zygisk-Il2CppDumper is globally DISABLED via property persist.il2cppdumper.enabled=0");
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        if (!package_name || strlen(package_name) == 0) {
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        bool match = false;
        // Default target
        if (strcmp(package_name, "com.dts.freefireth") == 0) {
            match = true;
        }

        mkdir("/sdcard/Bo_dump", 0777);

        // Multi-line package list from /sdcard/Bo_dump/target.txt
        FILE *tf = fopen("/sdcard/Bo_dump/target.txt", "r");
        if (!tf) {
            // Auto-create default target.txt
            FILE *defF = fopen("/sdcard/Bo_dump/target.txt", "w");
            if (defF) {
                fputs("# Target packages to dump (one package per line)\n", defF);
                fputs("# Enter '*' to dump any Unity game\n", defF);
                fputs("com.dts.freefireth\n", defF);
                fclose(defF);
            }
        } else {
            char lineBuf[256];
            while (fgets(lineBuf, sizeof(lineBuf), tf)) {
                char *p = lineBuf;
                while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
                if (*p == '\0' || *p == '#') continue;
                char *end = p + strlen(p) - 1;
                while (end >= p && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
                    *end = '\0';
                    end--;
                }
                if (strcmp(p, "*") == 0 || strcmp(package_name, p) == 0) {
                    match = true;
                    break;
                }
            }
            fclose(tf);
        }

        if (match) {
            LOGI("detect game: %s", package_name);
            enable_hack = true;
            pkg_name = new char[strlen(package_name) + 1];
            strcpy(pkg_name, package_name);
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
