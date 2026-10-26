// Path: module/src/main/cpp/il2cpp_dump.cpp
#include "il2cpp_dump.h"
#include <dlfcn.h>
#include <link.h>
#include <elf.h>
#include <cstdlib>
#include <cstring>
#include <cinttypes>
#include <string>
#include <vector>
#include <map>
#include <set>
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

    const char *meta = reinterpret_cast<const char *>(header);
    auto entries = reinterpret_cast<const Il2CppStringLiteralEntry *>(meta + header->stringLiteralOffset);
    const char *stringData = meta + header->stringLiteralDataOffset;
    size_t count = header->stringLiteralCount / sizeof(Il2CppStringLiteralEntry);

    std::ofstream strFile(outPath, std::ios::out | std::ios::trunc);
    if (strFile.is_open()) strFile << "[\n";

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
    }
}

// =============================================================================
// BỘ QUÉT OFFSET HÀM LIBANOGS.SO (XUẤT RA ANOGS_DUMP.CS & ANOGS_SCRIPT.JSON)
// =============================================================================
struct AnogsPhdrData {
    uintptr_t         base = 0;
    const ElfW(Phdr) *phdr = nullptr;
    ElfW(Half)        phnum = 0;
    std::string       full_path;
};

static int find_anogs_phdr_cb(struct dl_phdr_info *info, size_t /*size*/, void *data) {
    if (!info->dlpi_name) return 0;
    if (strstr(info->dlpi_name, "libanogs.so")) {
        auto *out = reinterpret_cast<AnogsPhdrData *>(data);
        out->base      = (uintptr_t)info->dlpi_addr;
        out->phdr      = info->dlpi_phdr;
        out->phnum     = info->dlpi_phnum;
        out->full_path = info->dlpi_name;
        return 1;
    }
    return 0;
}

static size_t get_gnu_hash_sym_count(const uint32_t *gnu_hash) {
    if (!gnu_hash) return 0;
    uint32_t nbuckets  = gnu_hash[0];
    uint32_t symoffset = gnu_hash[1];
    uint32_t bloom_sz  = gnu_hash[2];
    const auto *bloom  = reinterpret_cast<const ElfW(Addr) *>(&gnu_hash[4]);
    const uint32_t *buckets = reinterpret_cast<const uint32_t *>(&bloom[bloom_sz]);
    const uint32_t *chains  = &buckets[nbuckets];

    uint32_t max_sym = 0;
    for (uint32_t i = 0; i < nbuckets; ++i) {
        if (buckets[i] > max_sym) max_sym = buckets[i];
    }
    if (max_sym < symoffset) return symoffset;
    while ((chains[max_sym - symoffset] & 1) == 0) {
        max_sym++;
    }
    return (size_t)max_sym + 1;
}

static void dump_anogs_offsets(const std::string &boDir) {
    AnogsPhdrData anogs{};
    dl_iterate_phdr(find_anogs_phdr_cb, &anogs);
    if (!anogs.base || !anogs.phdr) {
        write_log("[-] libanogs.so not found in dl_iterate_phdr");
        return;
    }

    write_log("[+] Scanning libanogs.so offsets at base 0x%" PRIxPTR " (%s)...",
              anogs.base, anogs.full_path.c_str());

    // 1. Quét PT_DYNAMIC để lấy toàn bộ hàm Exported trong .dynsym
    std::map<uintptr_t, std::string> exported_funcs;
    std::vector<std::pair<uintptr_t, uintptr_t>> exec_ranges;

    for (int i = 0; i < anogs.phnum; ++i) {
        const ElfW(Phdr) &ph = anogs.phdr[i];
        if (ph.p_type == PT_LOAD && (ph.p_flags & PF_X)) {
            uintptr_t seg_start = anogs.base + ph.p_vaddr;
            uintptr_t seg_end   = seg_start + ph.p_memsz;
            exec_ranges.emplace_back(seg_start, seg_end);
        } else if (ph.p_type == PT_DYNAMIC) {
            const auto *dyn = reinterpret_cast<const ElfW(Dyn) *>(anogs.base + ph.p_vaddr);
            const ElfW(Sym) *symtab = nullptr;
            const char *strtab      = nullptr;
            size_t syment_cnt       = 0;

            for (; dyn->d_tag != DT_NULL; ++dyn) {
                uintptr_t ptr = (dyn->d_un.d_ptr > anogs.base) ? dyn->d_un.d_ptr : (anogs.base + dyn->d_un.d_ptr);
                if (dyn->d_tag == DT_SYMTAB) {
                    symtab = reinterpret_cast<const ElfW(Sym) *>(ptr);
                } else if (dyn->d_tag == DT_STRTAB) {
                    strtab = reinterpret_cast<const char *>(ptr);
                } else if (dyn->d_tag == DT_HASH) {
                    const auto *hash = reinterpret_cast<const uint32_t *>(ptr);
                    syment_cnt = hash[1]; // nchain
                } else if (dyn->d_tag == DT_GNU_HASH && syment_cnt == 0) {
                    syment_cnt = get_gnu_hash_sym_count(reinterpret_cast<const uint32_t *>(ptr));
                }
            }

            if (symtab && strtab && syment_cnt > 0 && syment_cnt < 100000) {
                for (size_t idx = 0; idx < syment_cnt; ++idx) {
                    const ElfW(Sym) &sym = symtab[idx];
                    uint8_t st_type = ELF32_ST_TYPE(sym.st_info);
                    if ((st_type == STT_FUNC || st_type == STT_GNU_IFUNC) &&
                        sym.st_shndx != SHN_UNDEF && sym.st_value != 0) {
                        uintptr_t rva = (uintptr_t)(sym.st_value & ~1ULL); // Bỏ bit Thumb lẻ
                        const char *sname = strtab + sym.st_name;
                        if (sname && sname[0] != '\0') {
                            exported_funcs[rva] = sname;
                        }
                    }
                }
            }
        }
    }

    // 2. Quét phân vùng thực thi (.text) tìm toàn bộ hàm nội bộ sub_XXXXXX (Prologue + Đích nhảy BL)
    std::map<uintptr_t, uint32_t> internal_call_counts; // RVA -> số lần được gọi qua lệnh BL
    std::set<uintptr_t>           prologue_rvas;        // RVA có đầu hàm PUSH / STP

    for (const auto &range : exec_ranges) {
        uintptr_t start = range.first;
        uintptr_t end   = range.second;
        if (end <= start || (end - start) > 64 * 1024 * 1024) continue;

#if defined(__aarch64__)
        // Quét lệnh ARM64 (4 bytes aligned)
        for (uintptr_t pc = start; pc + 4 <= end; pc += 4) {
            uint32_t insn = *reinterpret_cast<const uint32_t *>(pc);
            uintptr_t cur_rva = pc - anogs.base;

            // Lệnh BL <imm26> (Opcode: 100101....................)
            if ((insn & 0xFC000000u) == 0x94000000u) {
                int32_t imm26 = (int32_t)(insn & 0x03FFFFFFu);
                if (imm26 & 0x02000000) imm26 |= ~0x03FFFFFF; // Sign-extend
                intptr_t offset = (intptr_t)imm26 * 4;
                uintptr_t target_va = pc + offset;
                if (target_va >= start && target_va < end) {
                    internal_call_counts[target_va - anogs.base]++;
                }
            }
            // Prologue ARM64: STP X29, X30, [SP, #...] hoặc SUB SP, SP, #... hoặc PACIASP (0xD503233F)
            if ((insn & 0x7FC07FFFu) == 0x29807BFDu ||
                (insn & 0x7FC07FFFu) == 0x29007BFDu ||
                insn == 0xD503233Fu) {
                prologue_rvas.insert(cur_rva);
            }
        }
#elif defined(__arm__)
        // Quét lệnh ARM32 / Thumb-2 (2 bytes aligned)
        for (uintptr_t pc = start; pc + 4 <= end; pc += 2) {
            uint16_t hw1 = *reinterpret_cast<const uint16_t *>(pc);
            uint16_t hw2 = *reinterpret_cast<const uint16_t *>(pc + 2);
            uintptr_t cur_rva = pc - anogs.base;

            // 1. Thumb-1 Prologue: PUSH {..., LR} (0xB500 .. 0xB5FF)
            if ((hw1 & 0xFF00u) == 0xB500u) {
                prologue_rvas.insert(cur_rva);
            }
            // 2. Thumb-2 Prologue: PUSH.W {..., LR} (0xE92D 0x4...)
            else if (hw1 == 0xE92Du && (hw2 & 0x4000u) != 0) {
                prologue_rvas.insert(cur_rva);
            }
            // 3. ARM32 Prologue (4-byte aligned): PUSH {..., LR} (0xE92D4...)
            else if ((pc & 3) == 0) {
                uint32_t arm_insn = *reinterpret_cast<const uint32_t *>(pc);
                if ((arm_insn & 0xFFFF4000u) == 0xE92D4000u) {
                    prologue_rvas.insert(cur_rva);
                }
            }

            // 4. Thumb-2 BL <label> (32-bit instruction): hw1 = 11110 S imm10, hw2 = 11 J1 1 J2 imm11
            if ((hw1 & 0xF800u) == 0xF000u && (hw2 & 0xD000u) == 0xD000u) {
                uint32_t s    = (hw1 >> 10) & 1;
                uint32_t j1   = (hw2 >> 13) & 1;
                uint32_t j2   = (hw2 >> 11) & 1;
                uint32_t i1   = ~(j1 ^ s) & 1;
                uint32_t i2   = ~(j2 ^ s) & 1;
                uint32_t imm10 = hw1 & 0x03FFu;
                uint32_t imm11 = hw2 & 0x07FFu;
                int32_t  off   = (int32_t)((s << 24) | (i1 << 23) | (i2 << 22) | (imm10 << 12) | (imm11 << 1));
                if (s) off |= ~0x01FFFFFF; // Sign-extend 25-bit
                uintptr_t target_va = (pc + 4 + off) & ~1u;
                if (target_va >= start && target_va < end) {
                    internal_call_counts[target_va - anogs.base]++;
                }
                pc += 2; // Nhảy qua nửa sau của lệnh 32-bit
            }
        }
#endif
    }

    // 3. Hợp nhất toàn bộ hàm (Exported + Internal Subroutines) theo thứ tự RVA tăng dần
    std::set<uintptr_t> all_func_rvas;
    for (const auto &kv : exported_funcs) all_func_rvas.insert(kv.first);
    for (const auto &kv : internal_call_counts) {
        // Lấy các hàm được gọi bằng lệnh BL và có đầu hàm hợp lệ (hoặc được gọi >= 2 lần)
        if (prologue_rvas.count(kv.first) || kv.second >= 2) {
            all_func_rvas.insert(kv.first);
        }
    }
    for (uintptr_t rva : prologue_rvas) {
        all_func_rvas.insert(rva);
    }

    std::vector<uintptr_t> sorted_rvas(all_func_rvas.begin(), all_func_rvas.end());

    // 4. Xuất file /sdcard/Bo_dump/anogs_dump.cs (format giống hệt dump.cs)
    std::string cs_path   = boDir + "/anogs_dump.cs";
    std::string json_path = boDir + "/anogs_script.json";

    std::ofstream cs_out(cs_path, std::ios::out | std::ios::trunc);
    std::ofstream js_out(json_path, std::ios::out | std::ios::trunc);

    if (cs_out.is_open()) {
        cs_out << "// ============================================================================\n";
        cs_out << "// Library : libanogs.so (" << anogs.full_path << ")\n";
        cs_out << "// Base VA : 0x" << std::hex << anogs.base << "\n";
        cs_out << "// Exports : " << std::dec << exported_funcs.size()
               << " | Total Functions (Exported + Internal sub_XXX): " << sorted_rvas.size() << "\n";
        cs_out << "// ============================================================================\n\n";

        cs_out << "// Namespace: Garena.Anogs\n";
        cs_out << "public static class libanogs\n{\n";
        cs_out << "\t// ==================== 1. EXPORTED FUNCTIONS (.dynsym) ====================\n";
        for (const auto &kv : exported_funcs) {
            uintptr_t rva = kv.first;
            uintptr_t va  = anogs.base + rva;
            uint32_t calls = internal_call_counts.count(rva) ? internal_call_counts[rva] : 0;
            cs_out << "\t// RVA: 0x" << std::hex << rva << " Offset: 0x" << rva
                   << " VA: 0x" << va << " Calls: " << std::dec << calls << "\n";
            cs_out << "\tpublic static extern int " << kv.second << "();\n\n";
        }

        cs_out << "\t// ==================== 2. INTERNAL FUNCTIONS (sub_XXXXXX) ====================\n";
        for (size_t i = 0; i < sorted_rvas.size(); ++i) {
            uintptr_t rva = sorted_rvas[i];
            if (exported_funcs.count(rva)) continue;

            uintptr_t va  = anogs.base + rva;
            size_t fn_sz  = (i + 1 < sorted_rvas.size()) ? (sorted_rvas[i + 1] - rva) : 0;
            uint32_t calls = internal_call_counts.count(rva) ? internal_call_counts[rva] : 0;
            bool has_pro  = prologue_rvas.count(rva) != 0;

            // Chỉ in các hàm nội bộ có lệnh BL gọi tới hoặc có Prologue chuẩn
            if (calls == 0 && !has_pro) continue;

            cs_out << "\t// RVA: 0x" << std::hex << rva << " Offset: 0x" << rva
                   << " VA: 0x" << va
                   << " Size: 0x" << fn_sz
                   << " Calls: " << std::dec << calls
                   << (has_pro ? " [Prologue]" : " [BL_Target]") << "\n";
            cs_out << "\tpublic static int sub_" << std::hex << rva << "() { }\n\n";
        }
        cs_out << "}\n";
        cs_out.close();
    }

    if (js_out.is_open()) {
        js_out << "{\n  \"LibraryBase\": \"0x" << std::hex << anogs.base << "\",\n";
        js_out << "  \"ScriptMethod\": [\n";
        bool first = true;
        for (uintptr_t rva : sorted_rvas) {
            std::string fn_name;
            if (exported_funcs.count(rva)) {
                fn_name = "libanogs$$" + exported_funcs[rva];
            } else {
                char buf[64];
                snprintf(buf, sizeof(buf), "libanogs$$sub_%zx", (size_t)rva);
                fn_name = buf;
            }
            if (!first) js_out << ",\n";
            first = false;
            js_out << "    {\"Address\": " << std::dec << rva
                   << ", \"RVA\": \"0x" << std::hex << rva
                   << "\", \"Name\": \"" << json_escape(fn_name.c_str()) << "\"}";
        }
        js_out << "\n  ]\n}\n";
        js_out.close();
    }

    write_log("[+] libanogs.so offsets dumped: %zu exported + %zu total functions -> %s",
              exported_funcs.size(), sorted_rvas.size(), cs_path.c_str());
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
    write_log("[+] Starting IL2CPP + libanogs.so Offset Dump for %s...", outDir);
    show_toast("[Il2CppDumper] Dumping IL2CPP & libanogs.so offsets...");

    std::string boDir = "/sdcard/Bo_dump";
    mkdir(boDir.c_str(), 0777);

    // 1. Quét và xuất ngay toàn bộ offset hàm của libanogs.so ra anogs_dump.cs & anogs_script.json
    dump_anogs_offsets(boDir);

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

    auto outPath = boDir + "/dump.cs";
    auto scriptPath = boDir + "/script.json";
    auto strLiteralPath = boDir + "/stringliteral.json";

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

    if (scriptOpen) {
        scriptStream << "\n  ],\n  \"ScriptString\": [\n";
        dump_global_metadata_strings(strLiteralPath, scriptStream, scriptOpen);
        scriptStream << "\n  ],\n  \"ScriptMetadata\": [],\n  \"ScriptMetadataMethod\": []\n}\n";
        scriptStream.close();
    }

    write_log("[+] ALL DUMP OPERATIONS COMPLETED SUCCESSFULLY!");
    show_toast("[Il2CppDumper] SUCCESS! Saved dump.cs & anogs_dump.cs to /sdcard/Bo_dump/");
}
