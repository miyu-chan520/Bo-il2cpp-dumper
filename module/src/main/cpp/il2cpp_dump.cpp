//
// Modernized IL2CPP Dumper Engine
// Complete type resolution, recursive nested class discovery, constant literal extraction,
// runtime obfuscated string dumping from global-metadata and static fields, streaming output,
// script.json generation, and clean GC thread lifecycle management.
//

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

        // Dump constant literals (primitives, enums, strings)
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
            // Also inspect static string fields initialized at runtime
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

    // Write class definition to dump.cs
    outStream << dump_type(klass, image_name);

    // Export methods to script.json
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

    // Recursively traverse and dump nested types
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

    // Wait until il2cpp domain is ready (up to 60s)
    Il2CppDomain *domain = nullptr;
    int retry = 0;
    while (retry < 60) {
        if (il2cpp_domain_get) {
            domain = il2cpp_domain_get();
            if (domain) {
                write_log("[+] il2cpp domain is ready: %p", domain);
                break;
            }
        }
        sleep(1);
        retry++;
    }

    if (domain) {
        if (il2cpp_thread_attach) {
            s_attached_thread = il2cpp_thread_attach(domain);
            write_log("[+] Native thread attached to IL2CPP domain: %p", s_attached_thread);
        }
    } else {
        std::string err = "Timed out waiting for il2cpp domain (60s)";
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

    size_t size = 0;
    auto assemblies = il2cpp_domain_get_assemblies(domain, &size);
    if (!assemblies || size == 0) {
        std::string err = "No assemblies found in domain";
        write_log("[-] %s", err.c_str());
        show_toast(std::string("[Il2CppDumper] FAIL: ") + err);
        return;
    }

    // Ensure output directory exists: <outDir>/files/
    auto filesDir = std::string(outDir) + "/files";
    mkdir(filesDir.c_str(), 0777);
    auto outPath = filesDir + "/dump.cs";
    auto scriptPath = filesDir + "/script.json";
    auto strLiteralPath = filesDir + "/stringliteral.json";

    write_log("[+] Found %zu assemblies. Opening streaming output files...", size);

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

    // Dump global-metadata strings into ScriptString array in script.json and separate stringliteral.json
    if (scriptOpen) {
        scriptStream << "\n  ],\n  \"ScriptString\": [\n";
        dump_global_metadata_strings(strLiteralPath, scriptStream, scriptOpen);
        scriptStream << "\n  ],\n  \"ScriptMetadata\": [],\n  \"ScriptMetadataMethod\": []\n}\n";
        scriptStream.close();
        write_log("[+] script.json generated successfully at %s", scriptPath.c_str());
    }

    // Detach current thread cleanly from IL2CPP GC runtime
    if (il2cpp_thread_detach && s_attached_thread) {
        il2cpp_thread_detach(s_attached_thread);
        s_attached_thread = nullptr;
        write_log("[+] Native thread detached from IL2CPP GC runtime successfully");
    }

    // Mirror to /sdcard/Download/ for easy user access
    std::string dlPath = "/sdcard/Download/dump.cs";
    std::ifstream src(outPath, std::ios::binary);
    std::ofstream dst(dlPath, std::ios::binary);
    if (src.is_open() && dst.is_open()) {
        dst << src.rdbuf();
        write_log("[+] dump.cs copied to %s", dlPath.c_str());
    }

    if (scriptOpen) {
        std::string dlScript = "/sdcard/Download/script.json";
        std::ifstream srcS(scriptPath, std::ios::binary);
        std::ofstream dstS(dlScript, std::ios::binary);
        if (srcS.is_open() && dstS.is_open()) {
            dstS << srcS.rdbuf();
            write_log("[+] script.json copied to %s", dlScript.c_str());
        }
    }

    std::string dlStr = "/sdcard/Download/stringliteral.json";
    std::ifstream srcStr(strLiteralPath, std::ios::binary);
    std::ofstream dstStr(dlStr, std::ios::binary);
    if (srcStr.is_open() && dstStr.is_open()) {
        dstStr << srcStr.rdbuf();
        write_log("[+] stringliteral.json copied to %s", dlStr.c_str());
    }

    write_log("[+] ALL DUMP OPERATIONS COMPLETED SUCCESSFULLY!");
    show_toast("[Il2CppDumper] SUCCESS! Saved to /sdcard/Download/");
}
