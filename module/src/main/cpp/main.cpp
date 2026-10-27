#include <cstring>
#include <thread>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cinttypes>
#include <vector>
#include <string>
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
        const char *package_name = nullptr;
        const char *app_data_dir = nullptr;
        if (args->nice_name) {
            package_name = env->GetStringUTFChars(args->nice_name, nullptr);
        }
        if (args->app_data_dir) {
            app_data_dir = env->GetStringUTFChars(args->app_data_dir, nullptr);
        }

        preSpecialize(package_name, app_data_dir);

        if (package_name) {
            env->ReleaseStringUTFChars(args->nice_name, package_name);
        }
        if (app_data_dir) {
            env->ReleaseStringUTFChars(args->app_data_dir, app_data_dir);
        }
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
    bool enable_hack = false;
    char *pkg_name = nullptr;
    char *game_data_dir = nullptr;
    void *data = nullptr;
    size_t length = 0;

    bool is_dumper_enabled(int dirfd) {
        if (dirfd >= 0) {
            int dfd = openat(dirfd, "disable", O_RDONLY);
            if (dfd >= 0) {
                close(dfd);
                return false;
            }
        }
        if (access("/data/adb/modules/zygisk_il2cppdumper/disable", F_OK) == 0) {
            return false;
        }
        return true;
    }

    bool is_matching(const char *pkg, const char *target) {
        if (!pkg || !target) return false;
        if (strcmp(target, "*") == 0) return true;
        size_t tlen = strlen(target);
        if (tlen == 0) return false;
        if (strcmp(pkg, target) == 0) return true;
        if (strncmp(pkg, target, tlen) == 0 && pkg[tlen] == ':') return true;
        return false;
    }

    void preSpecialize(const char *package_name, const char *app_data_dir) {
        int dirfd = api->getModuleDir();

        if (!is_dumper_enabled(dirfd)) {
            LOGI("Zygisk-Il2CppDumper is DISABLED by /data/adb/modules/zygisk_il2cppdumper/disable");
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        if (!package_name || strlen(package_name) == 0) {
            if (app_data_dir && strlen(app_data_dir) > 0) {
                const char *last = strrchr(app_data_dir, '/');
                if (last && *(last + 1)) {
                    package_name = last + 1;
                }
            }
        }

        if (!package_name || strlen(package_name) == 0) {
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        bool match = false;

        // Default target check
        if (is_matching(package_name, "com.dts.freefireth")) {
            match = true;
        }

        // Try reading target.txt from all accessible locations
        std::vector<std::string> targetPaths = {
            "/data/media/0/Bo_dump/target.txt",
            "/sdcard/Bo_dump/target.txt",
            "/data/adb/modules/zygisk_il2cppdumper/target.txt"
        };

        FILE *tf = nullptr;
        for (const auto &tp : targetPaths) {
            tf = fopen(tp.c_str(), "r");
            if (tf) break;
        }

        if (!tf && dirfd >= 0) {
            int tfd = openat(dirfd, "target.txt", O_RDONLY);
            if (tfd >= 0) {
                tf = fdopen(tfd, "r");
            }
        }

        if (tf) {
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
                if (strlen(p) == 0) continue;

                if (strcmp(p, "*") == 0) {
                    if (strncmp(package_name, "com.android.", 12) != 0 &&
                        strncmp(package_name, "com.google.android.", 19) != 0 &&
                        strcmp(package_name, "system_server") != 0) {
                        match = true;
                        break;
                    }
                } else if (is_matching(package_name, p)) {
                    match = true;
                    break;
                }
            }
            fclose(tf);
        }

        if (match) {
            LOGI("Zygisk-Il2CppDumper detected target game: %s", package_name);
            enable_hack = true;
            pkg_name = strdup(package_name);
            game_data_dir = app_data_dir ? strdup(app_data_dir) : strdup("/data/data/unknown");

#if defined(__i386__)
            auto path = "zygisk/armeabi-v7a.so";
#endif
#if defined(__x86_64__)
            auto path = "zygisk/arm64-v8a.so";
#endif
#if defined(__i386__) || defined(__x86_64__)
            if (dirfd != -1) {
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
            }
#endif
        } else {
            api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
        }
    }
};

REGISTER_ZYGISK_MODULE(MyModule)
