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
#include <dirent.h>
#include <unordered_set>
#include <link.h>
#include <elf.h>
#include <map>
#include <iomanip>
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
    std::ofstream &offsetStream,
    bool offsetOpen,
    bool &firstMethod,
    std::unordered_set<const Il2CppClass *> &visitedClasses
) {
    if (!klass || visitedClasses.count(klass)) return;
    visitedClasses.insert(klass);

    outStream << dump_type(klass, image_name);

    const char *ns = il2cpp_class_get_namespace(klass);
    const char *cls = il2cpp_class_get_name(klass);
    std::string fullCls = (ns && strlen(ns) > 0) ? (std::string(ns) + "." + (cls ? cls : "unknown")) : (cls ? cls : "unknown");

    if (offsetOpen) {
        // Dynamic field offset extraction (independent of updates)
        void *fIter = nullptr;
        while (auto field = il2cpp_class_get_fields(klass, &fIter)) {
            const char *fName = il2cpp_field_get_name(field);
            auto field_type = il2cpp_field_get_type(field);
            std::string fType = get_type_name(field_type);
            size_t fOff = il2cpp_field_get_offset ? il2cpp_field_get_offset(field) : 0;
            offsetStream << fullCls << "::" << (fName ? fName : "unknown")
                         << " = 0x" << std::hex << fOff << "; // " << fType << "\n";
        }

        // Dynamic method RVA extraction (independent of updates)
        void *mIterOff = nullptr;
        while (auto method = il2cpp_class_get_methods(klass, &mIterOff)) {
            if (method->methodPointer) {
                uint64_t rva = (uint64_t) method->methodPointer - il2cpp_base;
                uint64_t va = (uint64_t) method->methodPointer;
                const char *mName = il2cpp_method_get_name(method);
                auto return_type = il2cpp_method_get_return_type(method);
                std::string ret_name = get_type_name(return_type);
                offsetStream << fullCls << "$$" << (mName ? mName : "unknown")
                             << " = 0x" << std::hex << rva << "; // VA: 0x" << va
                             << " | " << ret_name << " " << (mName ? mName : "unknown") << "()\n";
            }
        }
    }

    if (scriptOpen) {
        void *mIter = nullptr;
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
            dump_class_with_nested(nested, image_name, outStream, scriptStream, scriptOpen, offsetStream, offsetOpen, firstMethod, visitedClasses);
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
    }
    if (il2cpp_base == 0) {
        FILE *maps = fopen("/proc/self/maps", "r");
        if (maps) {
            char line[512];
            while (fgets(line, sizeof(line), maps)) {
                if (strstr(line, "libil2cpp.so")) {
                    uintptr_t start = 0;
                    if (sscanf(line, "%" SCNxPTR "-", &start) == 1) {
                        il2cpp_base = start;
                        break;
                    }
                }
            }
            fclose(maps);
        }
    }
    write_log("[+] il2cpp_base: 0x%" PRIx64, il2cpp_base);

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

struct NativeExportContext {
    std::ofstream *stream;
    size_t count;
};

static int phdr_dump_callback(struct dl_phdr_info *info, size_t size, void *data) {
    auto ctx = static_cast<NativeExportContext *>(data);
    if (!info || !info->dlpi_name || !ctx->stream) return 0;

    const char *path = info->dlpi_name;
    if (strlen(path) == 0) return 0;

    const char *baseName = strrchr(path, '/');
    baseName = baseName ? (baseName + 1) : path;

    bool isTarget = (strstr(baseName, "anogs") != nullptr ||
                     strstr(baseName, "il2cpp") != nullptr ||
                     strstr(baseName, "unity") != nullptr ||
                     strstr(baseName, "main") != nullptr ||
                     strstr(baseName, "Game") != nullptr);
    if (!isTarget && strstr(path, "/data/app/") == nullptr) {
        return 0;
    }

    ElfW(Addr) load_bias = info->dlpi_addr;
    const ElfW(Phdr) *phdr = info->dlpi_phdr;
    ElfW(Half) phnum = info->dlpi_phnum;

    auto resolve_addr = [&](ElfW(Addr) p) -> uintptr_t {
        if (p == 0) return 0;
        if (p < load_bias) return static_cast<uintptr_t>(load_bias + p);
        return static_cast<uintptr_t>(p);
    };

    const ElfW(Dyn) *dyn = nullptr;
    for (ElfW(Half) i = 0; i < phnum; ++i) {
        if (phdr[i].p_type == PT_DYNAMIC) {
            dyn = reinterpret_cast<const ElfW(Dyn) *>(load_bias + phdr[i].p_vaddr);
            break;
        }
    }
    if (!dyn) return 0;

    const ElfW(Sym) *symtab = nullptr;
    const char *strtab = nullptr;
    size_t nchain = 0;
    ElfW(Addr) dt_init = 0;
    ElfW(Addr) dt_init_array = 0;
    size_t dt_init_arraysz = 0;

    for (const ElfW(Dyn) *d = dyn; d->d_tag != DT_NULL; ++d) {
        if (d->d_tag == DT_SYMTAB) {
            symtab = reinterpret_cast<const ElfW(Sym) *>(resolve_addr(d->d_un.d_ptr));
        } else if (d->d_tag == DT_STRTAB) {
            strtab = reinterpret_cast<const char *>(resolve_addr(d->d_un.d_ptr));
        } else if (d->d_tag == DT_HASH) {
            auto hash = reinterpret_cast<const uint32_t *>(resolve_addr(d->d_un.d_ptr));
            if (hash) {
                nchain = hash[1];
            }
        } else if (d->d_tag == DT_GNU_HASH) {
            auto gnu_hash = reinterpret_cast<const uint32_t *>(resolve_addr(d->d_un.d_ptr));
            if (gnu_hash) {
                uint32_t nbuckets = gnu_hash[0];
                uint32_t symoffset = gnu_hash[1];
                uint32_t bloom_size = gnu_hash[2];
                const uint32_t *buckets = &gnu_hash[4 + (sizeof(void *) / 4) * bloom_size];
                const uint32_t *chains = &buckets[nbuckets];

                uint32_t max_sym = symoffset;
                for (uint32_t b = 0; b < nbuckets; ++b) {
                    uint32_t sym_idx = buckets[b];
                    if (sym_idx >= symoffset) {
                        while (true) {
                            if (sym_idx > max_sym) max_sym = sym_idx;
                            uint32_t chain_val = chains[sym_idx - symoffset];
                            if (chain_val & 1) break;
                            sym_idx++;
                        }
                    }
                }
                if (max_sym + 1 > nchain) {
                    nchain = max_sym + 1;
                }
            }
        } else if (d->d_tag == DT_INIT) {
            dt_init = d->d_un.d_ptr;
        } else if (d->d_tag == DT_INIT_ARRAY) {
            dt_init_array = d->d_un.d_ptr;
        } else if (d->d_tag == DT_INIT_ARRAYSZ) {
            dt_init_arraysz = d->d_un.d_val;
        }
    }

    *ctx->stream << "\n// ============================================================================\n";
    *ctx->stream << "// Module: " << baseName << " | Base: 0x" << std::hex << load_bias << "\n";
    *ctx->stream << "// Path: " << path << "\n";
    *ctx->stream << "// ============================================================================\n";

    bool isAnogs = (strstr(baseName, "anogs") != nullptr);

    if (isAnogs) {
        if (dt_init != 0) {
            uintptr_t init_va = resolve_addr(dt_init);
            uint64_t init_rva = (init_va >= load_bias) ? (init_va - load_bias) : dt_init;
            *ctx->stream << "ANOGS_DT_INIT = 0x" << std::hex << init_rva << "; // Offset: 0x" << init_rva << " | VA: 0x" << init_va << " (Constructor Entry)\n";
            ctx->count++;
        }
        if (dt_init_array != 0 && dt_init_arraysz > 0) {
            uintptr_t arr_va = resolve_addr(dt_init_array);
            size_t num_funcs = dt_init_arraysz / sizeof(void *);
            auto funcs = reinterpret_cast<const uintptr_t *>(arr_va);
            for (size_t k = 0; k < num_funcs; ++k) {
                uintptr_t fn = funcs[k];
                if (fn != 0 && fn != static_cast<uintptr_t>(-1)) {
                    uint64_t fn_rva = (fn >= load_bias) ? (fn - load_bias) : fn;
                    *ctx->stream << "ANOGS_INIT_ARRAY_" << std::dec << k << " = 0x" << std::hex << fn_rva
                                 << "; // Offset: 0x" << fn_rva << " | VA: 0x" << fn << " (Constructor #" << std::dec << k << ")\n";
                    ctx->count++;
                }
            }
        }
    }

    if (symtab && strtab && nchain > 0) {
        for (size_t i = 1; i < nchain; ++i) {
            const auto &sym = symtab[i];
            if (sym.st_name != 0 && sym.st_value != 0 && sym.st_shndx != SHN_UNDEF) {
                const char *symName = strtab + sym.st_name;
                uint64_t rva = sym.st_value;
                uint64_t va = load_bias + sym.st_value;

                if (isAnogs) {
                    *ctx->stream << "ANOGS_" << symName << " = 0x" << std::hex << rva << "; // Offset: 0x" << rva << " | VA: 0x" << va << "\n";
                } else {
                    *ctx->stream << "[" << baseName << "] " << symName << " = 0x" << std::hex << rva << "; // Offset: 0x" << rva << " | VA: 0x" << va << "\n";
                }
                ctx->count++;
            }
        }
    }

    if (isAnogs) {
        static const char *kSecKeywords[] = {
            "ptrace", "tracer", "/proc/", "frida", "xposed", "magisk", "detect",
            "scan", "root", "emulator", "debug", "inject", "hook", "il2cpp",
            "dumper", "zygisk", "socket", "connect", "sendto", "process_vm"
        };
        *ctx->stream << "\n// --- Security Signatures & Strings in libanogs.so ---\n";
        for (ElfW(Half) i = 0; i < phnum; ++i) {
            if (phdr[i].p_type == PT_LOAD && (phdr[i].p_flags & PF_R)) {
                const char *seg_start = reinterpret_cast<const char *>(load_bias + phdr[i].p_vaddr);
                size_t seg_memsz = phdr[i].p_memsz;
                size_t pos = 0;
                while (pos < seg_memsz) {
                    while (pos < seg_memsz && (seg_start[pos] < 32 || (unsigned char)seg_start[pos] > 126)) {
                        pos++;
                    }
                    size_t str_start = pos;
                    while (pos < seg_memsz && (seg_start[pos] >= 32 && (unsigned char)seg_start[pos] <= 126)) {
                        pos++;
                    }
                    size_t str_len = pos - str_start;
                    if (str_len >= 4 && pos < seg_memsz && seg_start[pos] == '\0') {
                        std::string foundStr(seg_start + str_start, str_len);
                        for (const char *kw : kSecKeywords) {
                            if (strcasestr(foundStr.c_str(), kw) != nullptr) {
                                uint64_t str_rva = phdr[i].p_vaddr + str_start;
                                uint64_t str_va = reinterpret_cast<uintptr_t>(seg_start + str_start);
                                *ctx->stream << "ANOGS_STR_" << kw << " = 0x" << std::hex << str_rva
                                             << "; // Offset: 0x" << str_rva << " | VA: 0x" << str_va
                                             << " (\"" << json_escape(foundStr.c_str(), foundStr.length()) << "\")\n";
                                break;
                            }
                        }
                    }
                    pos++;
                }
            }
        }
    }
    ctx->stream->flush();
    return 0;
}

static void dump_native_exports(const std::string &outPath) {
    write_log("[+] Extracting native export symbols from loaded modules...");
    std::ofstream expStream(outPath, std::ios::out | std::ios::trunc);
    if (!expStream.is_open()) {
        write_log("[-] Failed to open %s for writing native exports", outPath.c_str());
        return;
    }

    std::vector<char> expBuf(65536);
    expStream.rdbuf()->pubsetbuf(expBuf.data(), expBuf.size());

    expStream << "// ============================================================================\n";
    expStream << "// Native ELF Export Symbols with Relative Offsets (RVA) and Virtual Addresses (VA)\n";
    expStream << "// Generated by Zygisk-Il2CppDumper (Dynamic Edition)\n";
    expStream << "// Format:\n";
    expStream << "//   ANOGS_<Name> = 0x<Offset>; // Offset: 0x<Offset> | VA: 0x<VA>\n";
    expStream << "//   [Module] <Name> = 0x<Offset>; // Offset: 0x<Offset> | VA: 0x<VA>\n";
    expStream << "// ============================================================================\n";

    NativeExportContext ctx{&expStream, 0};
    xdl_iterate_phdr(phdr_dump_callback, &ctx, XDL_DEFAULT);

    expStream.close();
    write_log("[+] Native export symbols dump completed (%zu symbols exported) to %s", ctx.count, outPath.c_str());
}

static bool mkdir_p(const std::string &path, mode_t mode = 0777) {
    if (path.empty()) return false;
    char subpath[PATH_MAX];
    strncpy(subpath, path.c_str(), sizeof(subpath) - 1);
    subpath[sizeof(subpath) - 1] = '\0';

    for (char *p = subpath + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            if (access(subpath, F_OK) != 0) {
                mkdir(subpath, mode);
            }
            *p = '/';
        }
    }
    if (access(subpath, F_OK) != 0) {
        return mkdir(subpath, mode) == 0;
    }
    return true;
}

static void copy_file(const std::string &src, const std::string &dst) {
    std::ifstream in(src, std::ios::binary);
    if (!in.is_open()) return;
    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return;
    out << in.rdbuf();
}

void il2cpp_dump(const char *pkg_name, const char *game_data_dir) {
    std::string pkg = (pkg_name && strlen(pkg_name) > 0) ? pkg_name : "com.dts.freefireth";
    write_log("[+] Starting automated IL2CPP dump for %s...", pkg.c_str());
    show_toast(std::string("[Il2CppDumper] Dumping ") + pkg + ", please wait...");

    if (!il2cpp_domain_get || !il2cpp_domain_get_assemblies) {
        std::string err = "Required il2cpp APIs are missing";
        write_log("[-] %s", err.c_str());
        show_toast(std::string("[Il2CppDumper] FAIL: ") + err);
        return;
    }

    Il2CppDomain *domain = nullptr;
    const Il2CppAssembly **assemblies = nullptr;
    size_t size = 0;

    for (int retry = 0; retry < 60; ++retry) {
        domain = il2cpp_domain_get();
        if (domain) {
            if (il2cpp_thread_attach && !s_attached_thread) {
                s_attached_thread = il2cpp_thread_attach(domain);
                write_log("[+] Thread attached to IL2CPP domain: %p", s_attached_thread);
            }
            assemblies = il2cpp_domain_get_assemblies(domain, &size);
            if (assemblies && size > 0) {
                write_log("[+] IL2CPP domain ready (%zu assemblies loaded)", size);
                break;
            }
        }
        sleep(2);
    }

    if (!domain || !assemblies || size == 0) {
        std::string err = "Timed out waiting for il2cpp domain & assemblies (120s)";
        write_log("[-] %s", err.c_str());
        show_toast(std::string("[Il2CppDumper] FAIL: ") + err);
        return;
    }

    // Multi-candidate directory detection to guarantee write success on all Android versions
    std::vector<std::string> candidateDirs = {
        "/sdcard/Bo_dump/" + pkg,
        "/data/media/0/Bo_dump/" + pkg,
        "/sdcard/Download/Bo_dump/" + pkg,
        "/sdcard/Android/data/" + pkg + "/files/Bo_dump",
        std::string(game_data_dir ? game_data_dir : "/data/data/" + pkg) + "/files/Bo_dump"
    };

    std::string boDir = "";
    for (const auto &cand : candidateDirs) {
        mkdir_p(cand, 0777);

        std::string testFile = cand + "/.test_write";
        FILE *tf = fopen(testFile.c_str(), "w");
        if (tf) {
            fputs("ok", tf);
            fclose(tf);
            unlink(testFile.c_str());
            boDir = cand;
            write_log("[+] Selected output directory: %s", boDir.c_str());
            break;
        }
    }

    if (boDir.empty()) {
        std::string err = "All output directories failed to write (storage permission denied)";
        write_log("[-] %s", err.c_str());
        show_toast(std::string("[Il2CppDumper] FAIL: ") + err);
        return;
    }

    // Clean previous dump files in this directory so every dump session is fresh
    DIR *dir = opendir(boDir.c_str());
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
            std::string filePath = boDir + "/" + entry->d_name;
            unlink(filePath.c_str());
        }
        closedir(dir);
    }

    auto outPath = boDir + "/dump.cs";
    auto scriptPath = boDir + "/script.json";
    auto strLiteralPath = boDir + "/stringliteral.json";
    auto offsetPath = boDir + "/offset.txt";
    auto nativeExpPath = boDir + "/native_exports.txt";

    write_log("[+] Found %zu assemblies. Opening streaming buffered output files in %s...", size, boDir.c_str());

    std::vector<char> outBuf(65536);
    std::ofstream outStream(outPath, std::ios::out | std::ios::trunc);
    if (!outStream.is_open()) {
        LOGE("Failed to open %s for writing!", outPath.c_str());
        return;
    }
    outStream.rdbuf()->pubsetbuf(outBuf.data(), outBuf.size());

    std::vector<char> scriptBuf(65536);
    std::ofstream scriptStream(scriptPath, std::ios::out | std::ios::trunc);
    bool scriptOpen = scriptStream.is_open();
    if (scriptOpen) {
        scriptStream.rdbuf()->pubsetbuf(scriptBuf.data(), scriptBuf.size());
        scriptStream << "{\n  \"ScriptMethod\": [\n";
    }

    std::vector<char> offsetBuf(65536);
    std::ofstream offsetStream(offsetPath, std::ios::out | std::ios::trunc);
    bool offsetOpen = offsetStream.is_open();
    if (offsetOpen) {
        offsetStream.rdbuf()->pubsetbuf(offsetBuf.data(), offsetBuf.size());
        offsetStream << "// ============================================================================\n";
        offsetStream << "// IL2CPP Dynamic Runtime Offsets & RVAs (Auto-scanned from memory)\n";
        offsetStream << "// Target Package: " << pkg << "\n";
        offsetStream << "// Base Address: 0x" << std::hex << il2cpp_base << "\n";
        offsetStream << "// Format:\n";
        offsetStream << "//   Namespace.Class::FieldName = 0x<Offset>; // FieldType\n";
        offsetStream << "//   Namespace.Class$$MethodName = 0x<RVA>; // VA: 0x<VA> | ReturnType MethodName()\n";
        offsetStream << "// ============================================================================\n\n";
    }

    bool firstMethod = true;

    // Output all image headers
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
                    offsetStream,
                    offsetOpen,
                    firstMethod,
                    visitedClasses
                );
            }
            outStream.flush();
            if (scriptOpen) scriptStream.flush();
            if (offsetOpen) offsetStream.flush();
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

    if (offsetOpen) {
        offsetStream.close();
        write_log("[+] offset.txt generated successfully at %s", offsetPath.c_str());
    }

    // Generate native export symbols (including libanogs.so, libil2cpp.so, libunity.so)
    dump_native_exports(nativeExpPath);

    // Mirror copy to public sdcard/Download if candidate fallback was used
    std::vector<std::string> mirrorTargets = {
        "/sdcard/Bo_dump/" + pkg,
        "/sdcard/Download/Bo_dump/" + pkg
    };
    for (const auto &mirror : mirrorTargets) {
        if (mirror != boDir && mkdir_p(mirror, 0777)) {
            copy_file(outPath, mirror + "/dump.cs");
            copy_file(scriptPath, mirror + "/script.json");
            copy_file(strLiteralPath, mirror + "/stringliteral.json");
            copy_file(offsetPath, mirror + "/offset.txt");
            copy_file(nativeExpPath, mirror + "/native_exports.txt");
            write_log("[+] Mirror copy created at %s", mirror.c_str());
        }
    }

    write_log("[+] ALL DUMP OPERATIONS COMPLETED SUCCESSFULLY FOR %s AT %s!", pkg.c_str(), boDir.c_str());
    show_toast(std::string("[Il2CppDumper] SUCCESS! Saved to ") + boDir);
}
