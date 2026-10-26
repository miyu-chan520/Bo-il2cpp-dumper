//
// Created by Perfare on 2020/7/4.
// Modernized with Toast notification and clean truncated logging.
//

#ifndef ZYGISK_IL2CPPDUMPER_HACK_H
#define ZYGISK_IL2CPPDUMPER_HACK_H

#include <stddef.h>
#include <string>

void hack_prepare(const char *pkg_name, const char *game_data_dir, void *data, size_t length);
void hide_self_from_maps();
void show_toast(const std::string &message);
void init_log(const char *game_data_dir);
void write_log(const char *fmt, ...);

#endif //ZYGISK_IL2CPPDUMPER_HACK_H
