/*
 * Copyright 2014 Google Inc. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// independent from idl_parser, since this code is not needed for most clients

#include <unordered_set>

#include "flatbuffers/code_generators.h"
#include "flatbuffers/flatbuffers.h"
#include "flatbuffers/flatc.h"
#include "flatbuffers/idl.h"
#include "flatbuffers/util.h"

namespace flatbuffers {

// Make numerical literal with type-suffix.
// This function is only needed for C++! Other languages do not need it.
static inline std::string NumToStringCpp(std::string val, BaseType type) {
  // Avoid issues with -2147483648, -9223372036854775808.
  switch (type) {
    case BASE_TYPE_INT:
      return (val != "-2147483648") ? val : ("(-2147483647 - 1)");
    case BASE_TYPE_ULONG: return (val == "0") ? val : (val + "ULL");
    case BASE_TYPE_LONG:
      if (val == "-9223372036854775808")
        return "(-9223372036854775807LL - 1LL)";
      else
        return (val == "0") ? val : (val + "LL");
    default: return val;
  }
}

static std::string GenIncludeGuard(const std::string &file_name,
                                   const Namespace &name_space,
                                   const std::string &postfix = "") {
  // Generate include guard.
  std::string guard = file_name;
  // Remove any non-alpha-numeric characters that may appear in a filename.
  struct IsAlnum {
    bool operator()(char c) const { return !is_alnum(c); }
  };
  guard.erase(std::remove_if(guard.begin(), guard.end(), IsAlnum()),
              guard.end());
  guard = "FLATBUFFERS_GENERATED_" + guard;
  guard += "_";
  // For further uniqueness, also add the namespace.
  for (auto it = name_space.components.begin();
       it != name_space.components.end(); ++it) {
    guard += *it + "_";
  }
  // Anything extra to add to the guard?
  if (!postfix.empty()) { guard += postfix + "_"; }
  guard += "H_";
  std::transform(guard.begin(), guard.end(), guard.begin(), CharToUpper);
  return guard;
}

namespace cpp {

enum CppStandard { CPP_STD_X0 = 0, CPP_STD_11, CPP_STD_17 };

// Define a style of 'struct' constructor if it has 'Array' fields.
enum GenArrayArgMode {
  kArrayArgModeNone,        // don't generate initialization args
  kArrayArgModeSpanStatic,  // generate flatbuffers::span<T,N>
};

// Extension of IDLOptions for cpp-generator.
struct IDLOptionsCpp : public IDLOptions {
  // All fields start with 'g_' prefix to distinguish from the base IDLOptions.
  CppStandard g_cpp_std;    // Base version of C++ standard.
  bool g_only_fixed_enums;  // Generate underlaying type for all enums.

  IDLOptionsCpp(const IDLOptions &opts)
      : IDLOptions(opts), g_cpp_std(CPP_STD_11), g_only_fixed_enums(true) {}
};

class CppGenerator : public BaseGenerator {
 public:
  CppGenerator(const Parser &parser, const std::string &path,
               const std::string &file_name, IDLOptionsCpp opts)
      : BaseGenerator(parser, path, file_name, "", "::", "h"),
        cur_name_space_(nullptr),
        opts_(opts),
        float_const_gen_("std::numeric_limits<double>::",
                         "std::numeric_limits<float>::", "quiet_NaN()",
                         "infinity()") {
    static const char *const keywords[] = {
      "alignas",
      "alignof",
      "and",
      "and_eq",
      "asm",
      "atomic_cancel",
      "atomic_commit",
      "atomic_noexcept",
      "auto",
      "bitand",
      "bitor",
      "bool",
      "break",
      "case",
      "catch",
      "char",
      "char16_t",
      "char32_t",
      "class",
      "compl",
      "concept",
      "const",
      "constexpr",
      "const_cast",
      "continue",
      "co_await",
      "co_return",
      "co_yield",
      "decltype",
      "default",
      "delete",
      "do",
      "double",
      "dynamic_cast",
      "else",
      "enum",
      "explicit",
      "export",
      "extern",
      "false",
      "float",
      "for",
      "friend",
      "goto",
      "if",
      "import",
      "inline",
      "int",
      "long",
      "module",
      "mutable",
      "namespace",
      "new",
      "noexcept",
      "not",
      "not_eq",
      "nullptr",
      "operator",
      "or",
      "or_eq",
      "private",
      "protected",
      "public",
      "register",
      "reinterpret_cast",
      "requires",
      "return",
      "short",
      "signed",
      "sizeof",
      "static",
      "static_assert",
      "static_cast",
      "struct",
      "switch",
      "synchronized",
      "template",
      "this",
      "thread_local",
      "throw",
      "true",
      "try",
      "typedef",
      "typeid",
      "typename",
      "union",
      "unsigned",
      "using",
      "virtual",
      "void",
      "volatile",
      "wchar_t",
      "while",
      "xor",
      "xor_eq",
      nullptr,
    };
    for (auto kw = keywords; *kw; kw++) keywords_.insert(*kw);
  }

  void GenIncludeDependencies() {
    int num_includes = 0;
    if (opts_.generate_object_based_api) {
      for (auto it = parser_.native_included_files_.begin();
           it != parser_.native_included_files_.end(); ++it) {
        code_ += "#include \"" + *it + "\"";
        num_includes++;
      }
    }
    for (auto it = parser_.included_files_.begin();
         it != parser_.included_files_.end(); ++it) {
      if (it->second.empty()) continue;
      auto noext = flatbuffers::StripExtension(it->second);
      auto basename = flatbuffers::StripPath(noext);
      auto includeName =
          GeneratedFileName(opts_.include_prefix,
                            opts_.keep_include_path ? noext : basename, opts_);
      code_ += "#include \"" + includeName + "\"";
      num_includes++;
    }
    if (num_includes) code_ += "";
  }

  void GenExtraIncludes() {
    for (std::size_t i = 0; i < opts_.cpp_includes.size(); ++i) {
      code_ += "#include \"" + opts_.cpp_includes[i] + "\"";
    }
    if (!opts_.cpp_includes.empty()) { code_ += ""; }
  }

  std::string EscapeKeyword(const std::string &name) const {
    return keywords_.find(name) == keywords_.end() ? name : name + "_";
  }

  std::string Name(const Definition &def) const {
    return EscapeKeyword(def.name);
  }

  std::string Name(const EnumVal &ev) const { return EscapeKeyword(ev.name); }

  bool generate_bfbs_embed() {
    code_.Clear();
    code_ += "// " + std::string(FlatBuffersGeneratedWarning()) + "\n\n";

    // If we don't have a root struct definition,
    if (!parser_.root_struct_def_) {
      // put a comment in the output why there is no code generated.
      code_ += "// Binary schema not generated, no root struct found";
    } else {
      auto &struct_def = *parser_.root_struct_def_;
      const auto include_guard =
          GenIncludeGuard(file_name_, *struct_def.defined_namespace, "bfbs");

      code_ += "#ifndef " + include_guard;
      code_ += "#define " + include_guard;
      code_ += "";
      if (parser_.opts.gen_nullable) {
        code_ += "#pragma clang system_header\n\n";
      }

      SetNameSpace(struct_def.defined_namespace);
      auto name = Name(struct_def);
      code_.SetValue("STRUCT_NAME", name);

      // Create code to return the binary schema data.
      auto binary_schema_hex_text =
          BufferToHexText(parser_.builder_.GetBufferPointer(),
                          parser_.builder_.GetSize(), 105, "      ", "");

      code_ += "struct {{STRUCT_NAME}}BinarySchema {";
      code_ += "  static const uint8_t *data() {";
      code_ += "    // Buffer containing the binary schema.";
      code_ += "    static const uint8_t bfbsData[" +
               NumToString(parser_.builder_.GetSize()) + "] = {";
      code_ += binary_schema_hex_text;
      code_ += "    };";
      code_ += "    return bfbsData;";
      code_ += "  }";
      code_ += "  static size_t size() {";
      code_ += "    return " + NumToString(parser_.builder_.GetSize()) + ";";
      code_ += "  }";
      code_ += "  const uint8_t *begin() {";
      code_ += "    return data();";
      code_ += "  }";
      code_ += "  const uint8_t *end() {";
      code_ += "    return data() + size();";
      code_ += "  }";
      code_ += "};";
      code_ += "";

      if (cur_name_space_) SetNameSpace(nullptr);

      // Close the include guard.
      code_ += "#endif  // " + include_guard;
    }

    // We are just adding "_bfbs" to the generated filename.
    const auto file_path =
        GeneratedFileName(path_, file_name_ + "_bfbs", opts_);
    const auto final_code = code_.ToString();

    return SaveFile(file_path.c_str(), final_code, false);
  }

  // Iterate through all definitions we haven't generate code for (enums,
  // structs, and tables) and output them to a single file.
  bool generate() {
    code_.Clear();
    code_ += "// " + std::string(FlatBuffersGeneratedWarning()) + "\n\n";

    const auto include_guard =
        GenIncludeGuard(file_name_, *parser_.current_namespace_);
    code_ += "#ifndef " + include_guard;
    code_ += "#define " + include_guard;
    code_ += "";

    if (opts_.gen_nullable) { code_ += "#pragma clang system_header\n\n"; }

    code_ += "#include <contrib/libs/flatbuffers/include/flatbuffers/flatbuffers.h>";
    if (parser_.uses_flexbuffers_) {
      code_ += "#include <contrib/libs/flatbuffers/include/flatbuffers/flexbuffers.h>";
    }
    code_ += "";

    if (opts_.include_dependence_headers) { GenIncludeDependencies(); }
    GenExtraIncludes();

    FLATBUFFERS_ASSERT(!cur_name_space_);

    // Generate forward declarations for all structs/tables, since they may
    // have circular references.
    for (auto it = parser_.structs_.vec.begin();
         it != parser_.structs_.vec.end(); ++it) {
      const auto &struct_def = **it;
      if (!struct_def.generated) {
        SetNameSpace(struct_def.defined_namespace);
        code_ += "struct " + Name(struct_def) + ";";
        if (!struct_def.fixed) {
          code_ += "struct " + Name(struct_def) + "Builder;";
        }
        if (opts_.generate_object_based_api) {
          auto nativeName = NativeName(Name(struct_def), &struct_def, opts_);
          if (!struct_def.fixed) { code_ += "struct " + nativeName + ";"; }
        }
        code_ += "";
      }
    }

    // Generate forward declarations for all equal operators
    if (opts_.generate_object_based_api && opts_.gen_compare) {
      for (auto it = parser_.structs_.vec.begin();
           it != parser_.structs_.vec.end(); ++it) {
        const auto &struct_def = **it;
        if (!struct_def.generated) {
          SetNameSpace(struct_def.defined_namespace);
          auto nativeName = NativeName(Name(struct_def), &struct_def, opts_);
          code_ += "bool operator==(const " + nativeName + " &lhs, const " +
                   nativeName + " &rhs);";
          code_ += "bool operator!=(const " + nativeName + " &lhs, const " +
                   nativeName + " &rhs);";
        }
      }
      code_ += "";
    }

    // Generate preablmle code for mini reflection.
    if (opts_.mini_reflect != IDLOptions::kNone) {
      // To break cyclic dependencies, first pre-declare all tables/structs.
      for (auto it = parser_.structs_.vec.begin();
           it != parser_.structs_.vec.end(); ++it) {
        const auto &struct_def = **it;
        if (!struct_def.generated) {
          SetNameSpace(struct_def.defined_namespace);
          GenMiniReflectPre(&struct_def);
        }
      }
    }

    // Generate code for all the enum declarations.
    for (auto it = parser_.enums_.vec.begin(); it != parser_.enums_.vec.end();
         ++it) {
      const auto &enum_def = **it;
      if (!enum_def.generated) {
        SetNameSpace(enum_def.defined_namespace);
        GenEnum(enum_def);
      }
    }

    // Generate code for all structs, then all tables.
    for (auto it = parser_.structs_.vec.begin();
         it != parser_.structs_.vec.end(); ++it) {
      const auto &struct_def = **it;
      if (struct_def.fixed && !struct_def.generated) {
        SetNameSpace(struct_def.defined_namespace);
        GenStruct(struct_def);
      }
    }
    for (auto it = parser_.structs_.vec.begin();
         it != parser_.structs_.vec.end(); ++it) {
      const auto &struct_def = **it;
      if (!struct_def.fixed && !struct_def.generated) {
        SetNameSpace(struct_def.defined_namespace);
        GenTable(struct_def);
      }
    }
    for (auto it = parser_.structs_.vec.begin();
         it != parser_.structs_.vec.end(); ++it) {
      const auto &struct_def = **it;
      if (!struct_def.fixed && !struct_def.generated) {
        SetNameSpace(struct_def.defined_namespace);
        GenTablePost(struct_def);
      }
    }

    // Generate code for union verifiers.
    for (auto it = parser_.enums_.vec.begin(); it != parser_.enums_.vec.end();
         ++it) {
      const auto &enum_def = **it;
      if (enum_def.is_union && !enum_def.generated) {
        SetNameSpace(enum_def.defined_namespace);
        GenUnionPost(enum_def);
      }
    }

    // Generate code for mini reflection.
    if (opts_.mini_reflect != IDLOptions::kNone) {
      // Then the unions/enums that may refer to them.
      for (auto it = parser_.enums_.vec.begin(); it != parser_.enums_.vec.end();
           ++it) {
        const auto &enum_def = **it;
        if (!enum_def.generated) {
          SetNameSpace(enum_def.defined_namespace);
          GenMiniReflect(nullptr, &enum_def);
        }
      }
      // Then the full tables/structs.
      for (auto it = parser_.structs_.vec.begin();
           it != parser_.structs_.vec.end(); ++it) {
        const auto &struct_def = **it;
        if (!struct_def.generated) {
          SetNameSpace(struct_def.defined_namespace);
          GenMiniReflect(&struct_def, nullptr);
        }
      }
    }

    // Generate convenient global helper functions:
    if (parser_.root_struct_def_) {
      auto &struct_def = *parser_.root_struct_def_;
      SetNameSpace(struct_def.defined_namespace);
      auto name = Name(struct_def);
      auto qualified_name = cur_name_space_->GetFullyQualifiedName(name);
      auto cpp_name = TranslateNameSpace(qualified_name);

      code_.SetValue("STRUCT_NAME", name);
      code_.SetValue("CPP_NAME", cpp_name);
      code_.SetValue("NULLABLE_EXT", NullableExtension());

      // The root datatype accessor:
      code_ += "inline \\";
      code_ +=
          "const {{CPP_NAME}} *{{NULLABLE_EXT}}Get{{STRUCT_NAME}}(const void "
          "*buf) {";
      code_ += "  return flatbuffers::GetRoot<{{CPP_NAME}}>(buf);";
      code_ += "}";
      code_ += "";

      code_ += "inline \\";
      code_ +=
          "const {{CPP_NAME}} "
          "*{{NULLABLE_EXT}}GetSizePrefixed{{STRUCT_NAME}}(const void "
          "*buf) {";
      code_ += "  return flatbuffers::GetSizePrefixedRoot<{{CPP_NAME}}>(buf);";
      code_ += "}";
      code_ += "";

      if (opts_.mutable_buffer) {
        code_ += "inline \\";
        code_ += "{{STRUCT_NAME}} *GetMutable{{STRUCT_NAME}}(void *buf) {";
        code_ += "  return flatbuffers::GetMutableRoot<{{STRUCT_NAME}}>(buf);";
        code_ += "}";
        code_ += "";
      }

      if (parser_.file_identifier_.length()) {
        // Return the identifier
        code_ += "inline const char *{{STRUCT_NAME}}Identifier() {";
        code_ += "  return \"" + parser_.file_identifier_ + "\";";
        code_ += "}";
        code_ += "";

        // Check if a buffer has the identifier.
        code_ += "inline \\";
        code_ += "bool {{STRUCT_NAME}}BufferHasIdentifier(const void *buf) {";
        code_ += "  return flatbuffers::BufferHasIdentifier(";
        code_ += "      buf, {{STRUCT_NAME}}Identifier());";
        code_ += "}";
        code_ += "";
      }

      // The root verifier.
      if (parser_.file_identifier_.length()) {
        code_.SetValue("ID", name + "Identifier()");
      } else {
        code_.SetValue("ID", "nullptr");
      }

      code_ += "inline bool Verify{{STRUCT_NAME}}Buffer(";
      code_ += "    flatbuffers::Verifier &verifier) {";
      code_ += "  return verifier.VerifyBuffer<{{CPP_NAME}}>({{ID}});";
      code_ += "}";
      code_ += "";

      code_ += "inline bool VerifySizePrefixed{{STRUCT_NAME}}Buffer(";
      code_ += "    flatbuffers::Verifier &verifier) {";
      code_ +=
          "  return verifier.VerifySizePrefixedBuffer<{{CPP_NAME}}>({{ID}});";
      code_ += "}";
      code_ += "";

      if (parser_.file_extension_.length()) {
        // Return the extension
        code_ += "inline const char *{{STRUCT_NAME}}Extension() {";
        code_ += "  return \"" + parser_.file_extension_ + "\";";
        code_ += "}";
        code_ += "";
      }

      // Finish a buffer with a given root object:
      code_ += "inline void Finish{{STRUCT_NAME}}Buffer(";
      code_ += "    flatbuffers::FlatBufferBuilder &fbb,";
      code_ += "    flatbuffers::Offset<{{CPP_NAME}}> root) {";
      if (parser_.file_identifier_.length())
        code_ += "  fbb.Finish(root, {{STRUCT_NAME}}Identifier());";
      else
        code_ += "  fbb.Finish(root);";
      code_ += "}";
      code_ += "";

      code_ += "inline void FinishSizePrefixed{{STRUCT_NAME}}Buffer(";
      code_ += "    flatbuffers::FlatBufferBuilder &fbb,";
      code_ += "    flatbuffers::Offset<{{CPP_NAME}}> root) {";
      if (parser_.file_identifier_.length())
        code_ += "  fbb.FinishSizePrefixed(root, {{STRUCT_NAME}}Identifier());";
      else
        code_ += "  fbb.FinishSizePrefixed(root);";
      code_ += "}";
      code_ += "";

      if (opts_.generate_object_based_api) {
        // A convenient root unpack function.
        auto native_name = WrapNativeNameInNameSpace(struct_def, opts_);
        code_.SetValue("UNPACK_RETURN",
                       GenTypeNativePtr(native_name, nullptr, false));
        code_.SetValue("UNPACK_TYPE",
                       GenTypeNativePtr(native_name, nullptr, true));

        code_ += "inline {{UNPACK_RETURN}} UnPack{{STRUCT_NAME}}(";
        code_ += "    const void *buf,";
        code_ += "    const flatbuffers::resolver_function_t *res = nullptr) {";
        code_ += "  return {{UNPACK_TYPE}}\\";
        code_ += "(Get{{STRUCT_NAME}}(buf)->UnPack(res));";
        code_ += "}";
        code_ += "";

        code_ += "inline {{UNPACK_RETURN}} UnPackSizePrefixed{{STRUCT_NAME}}(";
        code_ += "    const void *buf,";
        code_ += "    const flatbuffers::resolver_function_t *res = nullptr) {";
        code_ += "  return {{UNPACK_TYPE}}\\";
        code_ += "(GetSizePrefixed{{STRUCT_NAME}}(buf)->UnPack(res));";
        code_ += "}";
        code_ += "";
      }
    }

    if (cur_name_space_) SetNameSpace(nullptr);

    // Close the include guard.
    code_ += "#endif  // " + include_guard;

    const auto file_path = GeneratedFileName(path_, file_name_, opts_);
    const auto final_code = code_.ToString();

    // Save the file and optionally generate the binary schema code.
    return SaveFile(file_path.c_str(), final_code, false) &&
           (!parser_.opts.binary_schema_gen_embed || generate_bfbs_embed());
  }

 private:
  CodeWriter code_;

  std::unordered_set<std::string> keywords_;

  // This tracks the current namespace so we can insert namespace declarations.
  const Namespace *cur_name_space_;

  const IDLOptionsCpp opts_;
  const TypedFloatConstantGenerator float_const_gen_;

  const Namespace *CurrentNameSpace() const { return cur_name_space_; }

  // Translates a qualified name in flatbuffer text format to the same name in
  // the equivalent C++ namespace.
  static std::string TranslateNameSpace(const std::string &qualified_name) {
    std::string cpp_qualified_name = qualified_name;
    size_t start_pos = 0;
    while ((start_pos = cpp_qualified_name.find('.', start_pos)) !=
           std::string::npos) {
      cpp_qualified_name.replace(start_pos, 1, "::");
    }
    return cpp_qualified_name;
  }

  bool TypeHasKey(const Type &type) {
    if (type.base_type != BASE_TYPE_STRUCT) { return false; }
    for (auto it = type.struct_def->fields.vec.begin();
         it != type.struct_def->fields.vec.end(); ++it) {
      const auto &field = **it;
      if (field.key) { return true; }
    }
    return false;
  }

  bool VectorElementUserFacing(const Type &type) const {
    return opts_.g_cpp_std >= cpp::CPP_STD_17 && opts_.g_only_fixed_enums &&
           IsEnum(type);
  }

  void GenComment(const std::vector<std::string> &dc, const char *prefix = "") {
    std::string text;
    ::flatbuffers::GenComment(dc, &text, nullptr, prefix);
    code_ += text + "\\";
  }

  // Return a C++ type from the table in idl.h
  std::string GenTypeBasic(const Type &type, bool user_facing_type) const {
    // clang-format off
    static const char *const ctypename[] = {
      #define FLATBUFFERS_TD(ENUM, IDLTYPE, CTYPE, ...) \
        #CTYPE,
        FLATBUFFERS_GEN_TYPES(FLATBUFFERS_TD)
      #undef FLATBUFFERS_TD
    };
    // clang-format on
    if (user_facing_type) {
      if (type.enum_def) return WrapInNameSpace(*type.enum_def);
      if (type.base_type == BASE_TYPE_BOOL) return "bool";
    }
    return ctypename[type.base_type];
  }

  // Return a C++ pointer type, specialized to the actual struct/table types,
  // and vector element types.
  std::string GenTypePointer(const Type &type) const {
    switch (type.base_type) {
      case BASE_TYPE_STRING: {
        return "flatbuffers::String";
      }
      case BASE_TYPE_VECTOR: {
        const auto type_name = GenTypeWire(
            type.VectorType(), "", VectorElementUserFacing(type.VectorType()));
        return "flatbuffers::Vector<" + type_name + ">";
      }
      case BASE_TYPE_STRUCT: {
        return WrapInNameSpace(*type.struct_def);
      }
      case BASE_TYPE_UNION:
        // fall through
      default: {
        return "void";
      }
    }
  }

  // Return a C++ type for any type (scalar/pointer) specifically for
  // building a flatbuffer.
  std::string GenTypeWire(const Type &type, const char *postfix,
                          bool user_facing_type) const {
    if (IsScalar(type.base_type)) {
      return GenTypeBasic(type, user_facing_type) + postfix;
    } else if (IsStruct(type)) {
      return "const " + GenTypePointer(type) + " *";
    } else {
      return "flatbuffers::Offset<" + GenTypePointer(type) + ">" + postfix;
    }
  }

  // Return a C++ type for any type (scalar/pointer) that reflects its
  // serialized size.
  std::string GenTypeSize(const Type &type) const {
    if (IsScalar(type.base_type)) {
      return GenTypeBasic(type, false);
    } else if (IsStruct(type)) {
      return GenTypePointer(type);
    } else {
      return "flatbuffers::uoffset_t";
    }
  }

  std::string NullableExtension() {
    return opts_.gen_nullable ? " _Nullable " : "";
  }

  static std::string NativeName(const std::string &name, const StructDef *sd,
                                const IDLOptions &opts) {
    return sd && !sd->fixed ? opts.object_prefix + name + opts.object_suffix
                            : name;
  }

  std::string WrapNativeNameInNameSpace(const StructDef &struct_def,
                                        const IDLOptions &opts) {
    return WrapInNameSpace(struct_def.defined_namespace,
                           NativeName(Name(struct_def), &struct_def, opts));
  }

  const std::string &PtrType(const FieldDef *field) {
    auto attr = field ? field->attributes.Lookup("cpp_ptr_type") : nullptr;
    return attr ? attr->constant : opts_.cpp_object_api_pointer_type;
  }

  const std::string NativeString(const FieldDef *field) {
    auto attr = field ? field->attributes.Lookup("cpp_str_type") : nullptr;
    auto &ret = attr ? attr->constant : opts_.cpp_object_api_string_type;
    if (ret.empty()) { return "std::string"; }
    return ret;
  }

  bool FlexibleStringConstructor(const FieldDef *field) {
    auto attr = field
                    ? (field->attributes.Lookup("cpp_str_flex_ctor") != nullptr)
                    : false;
    auto ret = attr ? attr : opts_.cpp_object_api_string_flexible_constructor;
    return ret && NativeString(field) !=
                      "std::string";  // Only for custom string types.
  }

  std::string GenTypeNativePtr(const std::string &type, const FieldDef *field,
                               bool is_constructor) {
    auto &ptr_type = PtrType(field);
    if (ptr_type != "naked") {
      return (ptr_type != "default_ptr_type"
                  ? ptr_type
                  : opts_.cpp_object_api_pointer_type) +
             "<" + type + ">";
    } else if (is_constructor) {
      return "";
    } else {
      return type + " *";
    }
  }

  std::string GenPtrGet(const FieldDef &field) {
    auto cpp_ptr_type_get = field.attributes.Lookup("cpp_ptr_type_get");
    if (cpp_ptr_type_get) return cpp_ptr_type_get->constant;
    auto &ptr_type = PtrType(&field);
    return ptr_type == "naked" ? "" : ".get()";
  }

  std::string GenOptionalNull() { return "flatbuffers::nullopt"; }

  std::string GenOptionalDecl(const Type &type) {
    return "flatbuffers::Optional<" + GenTypeBasic(type, true) + ">";
  }

  std::string GenTypeNative(const Type &type, bool invector,
                            const FieldDef &field) {
    switch (type.base_type) {
      case BASE_TYPE_STRING: {
        return NativeString(&field);
      }
      case BASE_TYPE_VECTOR: {
        const auto type_name = GenTypeNative(type.VectorType(), true, field);
        if (type.struct_def &&
            type.struct_def->attributes.Lookup("native_custom_alloc")) {
          auto native_custom_alloc =
              type.struct_def->attributes.Lookup("native_custom_alloc");
          return "std::vector<" + type_name + "," +
                 native_custom_alloc->constant + "<" + type_name + ">>";
        } else
          return "std::vector<" + type_name + ">";
      }
      case BASE_TYPE_STRUCT: {
        auto type_name = WrapInNameSpace(*type.struct_def);
        if (IsStruct(type)) {
          auto native_type = type.struct_def->attributes.Lookup("native_type");
          if (native_type) { type_name = native_type->constant; }
          if (invector || field.native_inline) {
            return type_name;
          } else {
            return GenTypeNativePtr(type_name, &field, false);
          }
        } else {
          return GenTypeNativePtr(
              WrapNativeNameInNameSpace(*type.struct_def, opts_), &field,
              false);
        }
      }
      case BASE_TYPE_UNION: {
        auto type_name = WrapInNameSpace(*type.enum_def);
        return type_name + "Union";
      }
      default: {
        return field.IsScalarOptional() ? GenOptionalDecl(type)
                                        : GenTypeBasic(type, true);
      }
    }
  }

  // Return a C++ type for any type (scalar/pointer) specifically for
  // using a flatbuffer.
  std::string GenTypeGet(const Type &type, const char *afterbasic,
                         const char *beforeptr, const char *afterptr,
                         bool user_facing_type) {
    if (IsScalar(type.base_type)) {
      return GenTypeBasic(type, user_facing_type) + afterbasic;
    } else if (IsArray(type)) {
      auto element_type = type.VectorType();
      // Check if enum arrays are used in C++ without specifying --scoped-enums
      if (IsEnum(element_type) && !opts_.g_only_fixed_enums) {
        LogCompilerError(
            "--scoped-enums must be enabled to use enum arrays in C++");
        FLATBUFFERS_ASSERT(true);
      }
      return beforeptr +
             (IsScalar(element_type.base_type)
                  ? GenTypeBasic(element_type, user_facing_type)
                  : GenTypePointer(element_type)) +
             afterptr;
    } else {
      return beforeptr + GenTypePointer(type) + afterptr;
    }
  }

  std::string GenTypeSpan(const Type &type, bool immutable, size_t extent) {
    // Generate "flatbuffers::span<const U, extent>".
    FLATBUFFERS_ASSERT(IsSeries(type) && "unexpected type");
    auto element_type = type.VectorType();
    std::string text = "flatbuffers::span<";
    text += immutable ? "const " : "";
    if (IsScalar(element_type.base_type)) {
      text += GenTypeBasic(element_type, IsEnum(element_type));
    } else {
      switch (element_type.base_type) {
        case BASE_TYPE_STRING: {
          text += "char";
          break;
        }
        case BASE_TYPE_STRUCT: {
          FLATBUFFERS_ASSERT(type.struct_def);
          text += WrapInNameSpace(*type.struct_def);
          break;
        }
        default:
          FLATBUFFERS_ASSERT(false && "unexpected element's type");
          break;
      }
    }
    if (extent != flatbuffers::dynamic_extent) {
      text += ", ";
      text += NumToString(extent);
    }
    text += "> ";
    return text;
  }

  std::string GenEnumValDecl(const EnumDef &enum_def,
                             const std::string &enum_val) const {
    return opts_.prefixed_enums ? Name(enum_def) + "_" + enum_val : enum_val;
  }

  std::string GetEnumValUse(const EnumDef &enum_def,
                            const EnumVal &enum_val) const {
    if (opts_.scoped_enums) {
      return Name(enum_def) + "::" + Name(enum_val);
    } else if (opts_.prefixed_enums) {
      return Name(enum_def) + "_" + Name(enum_val);
    } else {
      return Name(enum_val);
    }
  }

  std::string StripUnionType(const std::string &name) {
    return name.substr(0, name.size() - strlen(UnionTypeFieldSuffix()));
  }

  std::string GetUnionElement(const EnumVal &ev, bool native_type,
                              const IDLOptions &opts) {
    if (ev.union_type.base_type == BASE_TYPE_STRUCT) {
      auto name = ev.union_type.struct_def->name;
      if (native_type) {
        name = NativeName(name, ev.union_type.struct_def, opts);
      }
      return WrapInNameSpace(ev.union_type.struct_def->defined_namespace, name);
    } else if (IsString(ev.union_type)) {
      return native_type ? "std::string" : "flatbuffers::String";
    } else {
      FLATBUFFERS_ASSERT(false);
      return Name(ev);
    }
  }

  std::string UnionVerifySignature(const EnumDef &enum_def) {
    return "bool Verify" + Name(enum_def) +
           "(flatbuffers::Verifier &verifier, const void *obj, " +
           Name(enum_def) + " type)";
  }

  std::string UnionVectorVerifySignature(const EnumDef &enum_def) {
    return "bool Verify" + Name(enum_def) + "Vector" +
           "(flatbuffers::Verifier &verifier, " +
           "const flatbuffers::Vector<flatbuffers::Offset<void>> *values, " +
           "const flatbuffers::Vector<uint8_t> *types)";
  }

  std::string UnionUnPackSignature(const EnumDef &enum_def, bool inclass) {
    return (inclass ? "static " : "") + std::string("void *") +
           (inclass ? "" : Name(enum_def) + "Union::") +
           "UnPack(const void *obj, " + Name(enum_def) +
           " type, const flatbuffers::resolver_function_t *resolver)";
  }

  std::string UnionPackSignature(const EnumDef &enum_def, bool inclass) {
    return "flatbuffers::Offset<void> " +
           (inclass ? "" : Name(enum_def) + "Union::") +
           "Pack(flatbuffers::FlatBufferBuilder &_fbb, " +
           "const flatbuffers::rehasher_function_t *_rehasher" +
           (inclass ? " = nullptr" : "") + ") const";
  }

  std::string TableCreateSignature(const StructDef &struct_def, bool predecl,
                                   const IDLOptions &opts) {
    return "flatbuffers::Offset<" + Name(struct_def) + "> Create" +
           Name(struct_def) + "(flatbuffers::FlatBufferBuilder &_fbb, const " +
           NativeName(Name(struct_def), &struct_def, opts) +
           " *_o, const flatbuffers::rehasher_function_t *_rehasher" +
           (predecl ? " = nullptr" : "") + ")";
  }

  std::string TablePackSignature(const StructDef &struct_def, bool inclass,
                                 const IDLOptions &opts) {
    return std::string(inclass ? "static " : "") + "flatbuffers::Offset<" +
           Name(struct_def) + "> " + (inclass ? "" : Name(struct_def) + "::") +
           "Pack(flatbuffers::FlatBufferBuilder &_fbb, " + "const " +
           NativeName(Name(struct_def), &struct_def, opts) + "* _o, " +
           "const flatbuffers::rehasher_function_t *_rehasher" +
           (inclass ? " = nullptr" : "") + ")";
  }

  std::string TableUnPackSignature(const StructDef &struct_def, bool inclass,
                                   const IDLOptions &opts) {
    return NativeName(Name(struct_def), &struct_def, opts) + " *" +
           (inclass ? "" : Name(struct_def) + "::") +
           "UnPack(const flatbuffers::resolver_function_t *_resolver" +
           (inclass ? " = nullptr" : "") + ") const";
  }

  std::string TableUnPackToSignature(const StructDef &struct_def, bool inclass,
                                     const IDLOptions &opts) {
    return "void " + (inclass ? "" : Name(struct_def) + "::") + "UnPackTo(" +
           NativeName(Name(struct_def), &struct_def, opts) + " *" +
           "_o, const flatbuffers::resolver_function_t *_resolver" +
           (inclass ? " = nullptr" : "") + ") const";
  }

  void GenMiniReflectPre(const StructDef *struct_def) {
    code_.SetValue("NAME", struct_def->name);
    code_ += "inline const flatbuffers::TypeTable *{{NAME}}TypeTable();";
    code_ += "";
  }

  void GenMiniReflect(const StructDef *struct_def, const EnumDef *enum_def) {
    code_.SetValue("NAME", struct_def ? struct_def->name : enum_def->name);
    code_.SetValue("SEQ_TYPE",
                   struct_def ? (struct_def->fixed ? "ST_STRUCT" : "ST_TABLE")
                              : (enum_def->is_union ? "ST_UNION" : "ST_ENUM"));
    auto num_fields =
        struct_def ? struct_def->fields.vec.size() : enum_def->size();
    code_.SetValue("NUM_FIELDS", NumToString(num_fields));
    std::vector<std::string> names;
    std::vector<Type> types;

    if (struct_def) {
      for (auto it = struct_def->fields.vec.begin();
           it != struct_def->fields.vec.end(); ++it) {
        const auto &field = **it;
        names.push_back(Name(field));
        types.push_back(field.value.type);
      }
    } else {
      for (auto it = enum_def->Vals().begin(); it != enum_def->Vals().end();
           ++it) {
        const auto &ev = **it;
        names.push_back(Name(ev));
        types.push_back(enum_def->is_union ? ev.union_type
                                           : Type(enum_def->underlying_type));
      }
    }
    std::string ts;
    std::vector<std::string> type_refs;
    std::vector<uint16_t> array_sizes;
    for (auto it = types.begin(); it != types.end(); ++it) {
      auto &type = *it;
      if (!ts.empty()) ts += ",\n    ";
      auto is_vector = IsVector(type);
      auto is_array = IsArray(type);
      auto bt = is_vector || is_array ? type.element : type.base_type;
      auto et = IsScalar(bt) || bt == BASE_TYPE_STRING
                    ? bt - BASE_TYPE_UTYPE + ET_UTYPE
                    : ET_SEQUENCE;
      int ref_idx = -1;
      std::string ref_name =
          type.struct_def
              ? WrapInNameSpace(*type.struct_def)
              : type.enum_def ? WrapInNameSpace(*type.enum_def) : "";
      if (!ref_name.empty()) {
        auto rit = type_refs.begin();
        for (; rit != type_refs.end(); ++rit) {
          if (*rit == ref_name) {
            ref_idx = static_cast<int>(rit - type_refs.begin());
            break;
          }
        }
        if (rit == type_refs.end()) {
          ref_idx = static_cast<int>(type_refs.size());
          type_refs.push_back(ref_name);
        }
      }
      if (is_array) { array_sizes.push_back(type.fixed_length); }
      ts += "{ flatbuffers::" + std::string(ElementaryTypeNames()[et]) + ", " +
            NumToString(is_vector || is_array) + ", " + NumToString(ref_idx) +
            " }";
    }
    std::string rs;
    for (auto it = type_refs.begin(); it != type_refs.end(); ++it) {
      if (!rs.empty()) rs += ",\n    ";
      rs += *it + "TypeTable";
    }
    std::string as;
    for (auto it = array_sizes.begin(); it != array_sizes.end(); ++it) {
      as += NumToString(*it);
      as += ", ";
    }
    std::string ns;
    for (auto it = names.begin(); it != names.end(); ++it) {
      if (!ns.empty()) ns += ",\n    ";
      ns += "\"" + *it + "\"";
    }
    std::string vs;
    const auto consecutive_enum_from_zero =
        enum_def && enum_def->MinValue()->IsZero() &&
        ((enum_def->size() - 1) == enum_def->Distance());
    if (enum_def && !consecutive_enum_from_zero) {
      for (auto it = enum_def->Vals().begin(); it != enum_def->Vals().end();
           ++it) {
        const auto &ev = **it;
        if (!vs.empty()) vs += ", ";
        vs += NumToStringCpp(enum_def->ToString(ev),
                             enum_def->underlying_type.base_type);
      }
    } else if (struct_def && struct_def->fixed) {
      for (auto it = struct_def->fields.vec.begin();
           it != struct_def->fields.vec.end(); ++it) {
        const auto &field = **it;
        vs += NumToString(field.value.offset);
        vs += ", ";
      }
      vs += NumToString(struct_def->bytesize);
    }
    code_.SetValue("TYPES", ts);
    code_.SetValue("REFS", rs);
    code_.SetValue("ARRAYSIZES", as);
    code_.SetValue("NAMES", ns);
    code_.SetValue("VALUES", vs);
    code_ += "inline const flatbuffers::TypeTable *{{NAME}}TypeTable() {";
    if (num_fields) {
      code_ += "  static const flatbuffers::TypeCode type_codes[] = {";
      code_ += "    {{TYPES}}";
      code_ += "  };";
    }
    if (!type_refs.empty()) {
      code_ += "  static const flatbuffers::TypeFunction type_refs[] = {";
      code_ += "    {{REFS}}";
      code_ += "  };";
    }
    if (!as.empty()) {
      code_ += "  static const int16_t array_sizes[] = { {{ARRAYSIZES}} };";
    }
    if (!vs.empty()) {
      // Problem with uint64_t values greater than 9223372036854775807ULL.
      code_ += "  static const int64_t values[] = { {{VALUES}} };";
    }
    auto has_names =
        num_fields && opts_.mini_reflect == IDLOptions::kTypesAndNames;
    if (has_names) {
      code_ += "  static const char * const names[] = {";
      code_ += "    {{NAMES}}";
      code_ += "  };";
    }
    code_ += "  static const flatbuffers::TypeTable tt = {";
    code_ += std::string("    flatbuffers::{{SEQ_TYPE}}, {{NUM_FIELDS}}, ") +
             (num_fields ? "type_codes, " : "nullptr, ") +
             (!type_refs.empty() ? "type_refs, " : "nullptr, ") +
             (!as.empty() ? "array_sizes, " : "nullptr, ") +
             (!vs.empty() ? "values, " : "nullptr, ") +
             (has_names ? "names" : "nullptr");
    code_ += "  };";
    code_ += "  return &tt;";
    code_ += "}";
    code_ += "";
  }

  // Generate an enum declaration,
  // an enum string lookup table,
  // and an enum array of values

  void GenEnum(const EnumDef &enum_def) {
    code_.SetValue("ENUM_NAME", Name(enum_def));
    code_.SetValue("BASE_TYPE", GenTypeBasic(enum_def.underlying_type, false));

    GenComment(enum_def.doc_comment);
    code_ +=
        (opts_.scoped_enums ? "enum class " : "enum ") + Name(enum_def) + "\\";
    if (opts_.g_only_fixed_enums) { code_ += " : {{BASE_TYPE}}\\"; }
    code_ += " {";

    code_.SetValue("SEP", ",");
    auto add_sep = false;
    for (auto it = enum_def.Vals().begin(); it != enum_def.Vals().end(); ++it) {
      const auto &ev = **it;
      if (add_sep) code_ += "{{SEP}}";
      GenComment(ev.doc_comment, "  ");
      code_.SetValue("KEY", GenEnumValDecl(enum_def, Name(ev)));
      code_.SetValue("VALUE",
                     NumToStringCpp(enum_def.ToString(ev),
                                    enum_def.underlying_type.base_type));
      code_ += "  {{KEY}} = {{VALUE}}\\";
      add_sep = true;
    }
    const EnumVal *minv = enum_def.MinValue();
    const EnumVal *maxv = enum_def.MaxValue();

    if (opts_.scoped_enums || opts_.prefixed_enums) {
      FLATBUFFERS_ASSERT(minv && maxv);

      code_.SetValue("SEP", ",\n");
      if (enum_def.attributes.Lookup("bit_flags")) {
        code_.SetValue("KEY", GenEnumValDecl(enum_def, "NONE"));
        code_.SetValue("VALUE", "0");
        code_ += "{{SEP}}  {{KEY}} = {{VALUE}}\\";

        code_.SetValue("KEY", GenEnumValDecl(enum_def, "ANY"));
        code_.SetValue("VALUE",
                       NumToStringCpp(enum_def.AllFlags(),
                                      enum_def.underlying_type.base_type));
        code_ += "{{SEP}}  {{KEY}} = {{VALUE}}\\";
      } else {  // MIN & MAX are useless for bit_flags
        code_.SetValue("KEY", GenEnumValDecl(enum_def, "MIN"));
        code_.SetValue("VALUE", GenEnumValDecl(enum_def, Name(*minv)));
        code_ += "{{SEP}}  {{KEY}} = {{VALUE}}\\";

        code_.SetValue("KEY", GenEnumValDecl(enum_def, "MAX"));
        code_.SetValue("VALUE", GenEnumValDecl(enum_def, Name(*maxv)));
        code_ += "{{SEP}}  {{KEY}} = {{VALUE}}\\";
      }
    }
    code_ += "";
    code_ += "};";

    if (opts_.scoped_enums && enum_def.attributes.Lookup("bit_flags")) {
      code_ +=
          "FLATBUFFERS_DEFINE_BITMASK_OPERATORS({{ENUM_NAME}}, {{BASE_TYPE}})";
    }
    code_ += "";

    // Generate an array of all enumeration values
    auto num_fields = NumToString(enum_def.size());
    code_ += "inline const {{ENUM_NAME}} (&EnumValues{{ENUM_NAME}}())[" +
             num_fields + "] {";
    code_ += "  static const {{ENUM_NAME}} values[] = {";
    for (auto it = enum_def.Vals().begin(); it != enum_def.Vals().end(); ++it) {
      const auto &ev = **it;
      auto value = GetEnumValUse(enum_def, ev);
      auto suffix = *it != enum_def.Vals().back() ? "," : "";
      code_ += "    " + value + suffix;
    }
    code_ += "  };";
    code_ += "  return values;";
    code_ += "}";
    code_ += "";

    // Generate a generate string table for enum values.
    // Problem is, if values are very sparse that could generate really big
    // tables. Ideally in that case we generate a map lookup instead, but for
    // the moment we simply don't output a table at all.
    auto range = enum_def.Distance();
    // Average distance between values above which we consider a table
    // "too sparse". Change at will.
    static const uint64_t kMaxSparseness = 5;
    if (range / static_cast<uint64_t>(enum_def.size()) < kMaxSparseness) {
      code_ += "inline const char * const *EnumNames{{ENUM_NAME}}() {";
      code_ += "  static const char * const names[" +
               NumToString(range + 1 + 1) + "] = {";

      auto val = enum_def.Vals().front();
      for (auto it = enum_def.Vals().begin(); it != enum_def.Vals().end();
           ++it) {
        auto ev = *it;
        for (auto k = enum_def.Distance(val, ev); k > 1; --k) {
          code_ += "    \"\",";
        }
        val = ev;
        code_ += "    \"" + Name(*ev) + "\",";
      }
      code_ += "    nullptr";
      code_ += "  };";

      code_ += "  return names;";
      code_ += "}";
      code_ += "";

      code_ += "inline const char *EnumName{{ENUM_NAME}}({{ENUM_NAME}} e) {";

      code_ += "  if (flatbuffers::IsOutRange(e, " +
               GetEnumValUse(enum_def, *enum_def.MinValue()) + ", " +
               GetEnumValUse(enum_def, *enum_def.MaxValue()) +
               ")) return \"\";";

      code_ += "  const size_t index = static_cast<size_t>(e)\\";
      if (enum_def.MinValue()->IsNonZero()) {
        auto vals = GetEnumValUse(enum_def, *enum_def.MinValue());
        code_ += " - static_cast<size_t>(" + vals + ")\\";
      }
      code_ += ";";

      code_ += "  return EnumNames{{ENUM_NAME}}()[index];";
      code_ += "}";
      code_ += "";
    } else {
      code_ += "inline const char *EnumName{{ENUM_NAME}}({{ENUM_NAME}} e) {";

      code_ += "  switch (e) {";

      for (auto it = enum_def.Vals().begin(); it != enum_def.Vals().end();
           ++it) {
        const auto &ev = **it;
        code_ += "    case " + GetEnumValUse(enum_def, ev) + ": return \"" +
                 Name(ev) + "\";";
      }

      code_ += "    default: return \"\";";
      code_ += "  }";

      code_ += "}";
      code_ += "";
    }

    // Generate type traits for unions to map from a type to union enum value.
    if (enum_def.is_union && !enum_def.uses_multiple_type_instances) {
      for (auto it = enum_def.Vals().begin(); it != enum_def.Vals().end();
           ++it) {
        const auto &ev = **it;

        if (it == enum_def.Vals().begin()) {
          code_ += "template<typename T> struct {{ENUM_NAME}}Traits {";
        } else {
          auto name = GetUnionElement(ev, false, opts_);
          code_ += "template<> struct {{ENUM_NAME}}Traits<" + name + "> {";
        }

        auto value = GetEnumValUse(enum_def, ev);
        code_ += "  static const {{ENUM_NAME}} enum_value = " + value + ";";
        code_ += "};";
        code_ += "";
      }
    }

    if (opts_.generate_object_based_api && enum_def.is_union) {
      // Generate a union type
      code_.SetValue("NAME", Name(enum_def));
      FLATBUFFERS_ASSERT(enum_def.Lookup("NONE"));
      code_.SetValue("NONE", GetEnumValUse(enum_def, *enum_def.Lookup("NONE")));

      code_ += "struct {{NAME}}Union {";
      code_ += "  {{NAME}} type;";
      code_ += "  void *value;";
      code_ += "";
      code_ += "  {{NAME}}Union() : type({{NONE}}), value(nullptr) {}";
      code_ += "  {{NAME}}Union({{NAME}}Union&& u) FLATBUFFERS_NOEXCEPT :";
      code_ += "    type({{NONE}}), value(nullptr)";
      code_ += "    { std::swap(type, u.type); std::swap(value, u.value); }";
      code_ += "  {{NAME}}Union(const {{NAME}}Union &);";
      code_ += "  {{NAME}}Union &operator=(const {{NAME}}Union &u)";
      code_ +=
          "    { {{NAME}}Union t(u); std::swap(type, t.type); std::swap(value, "
          "t.value); return *this; }";
      code_ +=
          "  {{NAME}}Union &operator=({{NAME}}Union &&u) FLATBUFFERS_NOEXCEPT";
      code_ +=
          "    { std::swap(type, u.type); std::swap(value, u.value); return "
          "*this; }";
      code_ += "  ~{{NAME}}Union() { Reset(); }";
      code_ += "";
      code_ += "  void Reset();";
      code_ += "";
      if (!enum_def.uses_multiple_type_instances) {
        code_ += "#ifndef FLATBUFFERS_CPP98_STL";
        code_ += "  template <typename T>";
        code_ += "  void Set(T&& val) {";
        code_ += "    using RT = typename std::remove_reference<T>::type;";
        code_ += "    Reset();";
        code_ +=
            "    type = {{NAME}}Traits<typename RT::TableType>::enum_value;";
        code_ += "    if (type != {{NONE}}) {";
        code_ += "      value = new RT(std::forward<T>(val));";
        code_ += "    }";
        code_ += "  }";
        code_ += "#endif  // FLATBUFFERS_CPP98_STL";
        code_ += "";
      }
      code_ += "  " + UnionUnPackSignature(enum_def, true) + ";";
      code_ += "  " + UnionPackSignature(enum_def, true) + ";";
      code_ += "";

      for (auto it = enum_def.Vals().begin(); it != enum_def.Vals().end();
           ++it) {
        const auto &ev = **it;
        if (ev.IsZero()) { continue; }

        const auto native_type = GetUnionElement(ev, true, opts_);
        code_.SetValue("NATIVE_TYPE", native_type);
        code_.SetValue("NATIVE_NAME", Name(ev));
        code_.SetValue("NATIVE_ID", GetEnumValUse(enum_def, ev));

        code_ += "  {{NATIVE_TYPE}} *As{{NATIVE_NAME}}() {";
        code_ += "    return type == {{NATIVE_ID}} ?";
        code_ += "      reinterpret_cast<{{NATIVE_TYPE}} *>(value) : nullptr;";
        code_ += "  }";

        code_ += "  const {{NATIVE_TYPE}} *As{{NATIVE_NAME}}() const {";
        code_ += "    return type == {{NATIVE_ID}} ?";
        code_ +=
            "      reinterpret_cast<const {{NATIVE_TYPE}} *>(value) : nullptr;";
        code_ += "  }";
      }
      code_ += "};";
      code_ += "";

      if (opts_.gen_compare) {
        code_ += "";
        code_ +=
            "inline bool operator==(const {{NAME}}Union &lhs, const "
            "{{NAME}}Union &rhs) {";
        code_ += "  if (lhs.type != rhs.type) return false;";
        code_ += "  switch (lhs.type) {";

        for (auto it = enum_def.Vals().begin(); it != enum_def.Vals().end();
             ++it) {
          const auto &ev = **it;
          code_.SetValue("NATIVE_ID", GetEnumValUse(enum_def, ev));
          if (ev.IsNonZero()) {
            const auto native_type = GetUnionElement(ev, true, opts_);
            code_.SetValue("NATIVE_TYPE", native_type);
            code_ += "    case {{NATIVE_ID}}: {";
            code_ +=
                "      return *(reinterpret_cast<const {{NATIVE_TYPE}} "
                "*>(lhs.value)) ==";
            code_ +=
                "             *(reinterpret_cast<const {{NATIVE_TYPE}} "
                "*>(rhs.value));";
            code_ += "    }";
          } else {
            code_ += "    case {{NATIVE_ID}}: {";
            code_ += "      return true;";  // "NONE" enum value.
            code_ += "    }";
          }
        }
        code_ += "    default: {";
        code_ += "      return false;";
        code_ += "    }";
        code_ += "  }";
        code_ += "}";

        code_ += "";
        code_ +=
            "inline bool operator!=(const {{NAME}}Union &lhs, const "
            "{{NAME}}Union &rhs) {";
        code_ += "    return !(lhs == rhs);";
        code_ += "}";
        code_ += "";
      }
    }

    if (enum_def.is_union) {
      code_ += UnionVerifySignature(enum_def) + ";";
      code_ += UnionVectorVerifySignature(enum_def) + ";";
      code_ += "";
    }
  }

  void GenUnionPost(const EnumDef &enum_def) {
    // Generate a verifier function for this union that can be called by the
    // table verifier functions. It uses a switch case to select a specific
    // verifier function to call, this should be safe even if the union type
    // has been corrupted, since the verifiers will simply fail when called
    // on the wrong type.
    code_.SetValue("ENUM_NAME", Name(enum_def));

    code_ += "inline " + UnionVerifySignature(enum_def) + " {";
    code_ += "  switch (type) {";
    for (auto it = enum_def.Vals().begin(); it != enum_def.Vals().end(); ++it) {
      const auto &ev = **it;
      code_.SetValue("LABEL", GetEnumValUse(enum_def, ev));

      if (ev.IsNonZero()) {
        code_.SetValue("TYPE", GetUnionElement(ev, false, opts_));
        code_ += "    case {{LABEL}}: {";
        auto getptr =
            "      auto ptr = reinterpret_cast<const {{TYPE}} *>(obj);";
        if (ev.union_type.base_type == BASE_TYPE_STRUCT) {
          if (ev.union_type.struct_def->fixed) {
            code_ +=
                "      return verifier.Verify<{{TYPE}}>(static_cast<const "
                "uint8_t *>(obj), 0);";
          } else {
            code_ += getptr;
            code_ += "      return verifier.VerifyTable(ptr);";
          }
        } else if (IsString(ev.union_type)) {
          code_ += getptr;
          code_ += "      return verifier.VerifyString(ptr);";
        } else {
          FLATBUFFERS_ASSERT(false);
        }
        code_ += "    }";
      } else {
        code_ += "    case {{LABEL}}: {";
        code_ += "      return true;";  // "NONE" enum value.
        code_ += "    }";
      }
    }
    code_ += "    default: return true;";  // unknown values are OK.
    code_ += "  }";
    code_ += "}";
    code_ += "";

    code_ += "inline " + UnionVectorVerifySignature(enum_def) + " {";
    code_ += "  if (!values || !types) return !values && !types;";
    code_ += "  if (values->size() != types->size()) return false;";
    code_ += "  for (flatbuffers::uoffset_t i = 0; i < values->size(); ++i) {";
    code_ += "    if (!Verify" + Name(enum_def) + "(";
    code_ += "        verifier,  values->Get(i), types->GetEnum<" +
             Name(enum_def) + ">(i))) {";
    code_ += "      return false;";
    code_ += "    }";
    code_ += "  }";
    code_ += "  return true;";
    code_ += "}";
    code_ += "";

    if (opts_.generate_object_based_api) {
      // Generate union Unpack() and Pack() functions.
      code_ += "inline " + UnionUnPackSignature(enum_def, false) + " {";
      code_ += "  switch (type) {";
      for (auto it = enum_def.Vals().begin(); it != enum_def.Vals().end();
           ++it) {
        const auto &ev = **it;
        if (ev.IsZero()) { continue; }

        code_.SetValue("LABEL", GetEnumValUse(enum_def, ev));
        code_.SetValue("TYPE", GetUnionElement(ev, false, opts_));
        code_ += "    case {{LABEL}}: {";
        code_ += "      auto ptr = reinterpret_cast<const {{TYPE}} *>(obj);";
        if (ev.union_type.base_type == BASE_TYPE_STRUCT) {
          if (ev.union_type.struct_def->fixed) {
            code_ += "      return new " +
                     WrapInNameSpace(*ev.union_type.struct_def) + "(*ptr);";
          } else {
            code_ += "      return ptr->UnPack(resolver);";
          }
        } else if (IsString(ev.union_type)) {
          code_ += "      return new std::string(ptr->c_str(), ptr->size());";
        } else {
          FLATBUFFERS_ASSERT(false);
        }
        code_ += "    }";
      }
      code_ += "    default: return nullptr;";
      code_ += "  }";
      code_ += "}";
      code_ += "";

      code_ += "inline " + UnionPackSignature(enum_def, false) + " {";
      code_ += "  switch (type) {";
      for (auto it = enum_def.Vals().begin(); it != enum_def.Vals().end();
           ++it) {
        auto &ev = **it;
        if (ev.IsZero()) { continue; }

        code_.SetValue("LABEL", GetEnumValUse(enum_def, ev));
        code_.SetValue("TYPE", GetUnionElement(ev, true, opts_));
        code_ += "    case {{LABEL}}: {";
        code_ += "      auto ptr = reinterpret_cast<const {{TYPE}} *>(value);";
        if (ev.union_type.base_type == BASE_TYPE_STRUCT) {
          if (ev.union_type.struct_def->fixed) {
            code_ += "      return _fbb.CreateStruct(*ptr).Union();";
          } else {
            code_.SetValue("NAME", ev.union_type.struct_def->name);
            code_ +=
                "      return Create{{NAME}}(_fbb, ptr, _rehasher).Union();";
          }
        } else if (IsString(ev.union_type)) {
          code_ += "      return _fbb.CreateString(*ptr).Union();";
        } else {
          FLATBUFFERS_ASSERT(false);
        }
        code_ += "    }";
      }
      code_ += "    default: return 0;";
      code_ += "  }";
      code_ += "}";
      code_ += "";

      // Union copy constructor
      code_ +=
          "inline {{ENUM_NAME}}Union::{{ENUM_NAME}}Union(const "
          "{{ENUM_NAME}}Union &u) : type(u.type), value(nullptr) {";
      code_ += "  switch (type) {";
      for (auto it = enum_def.Vals().begin(); it != enum_def.Vals().end();
           ++it) {
        const auto &ev = **it;
        if (ev.IsZero()) { continue; }
        code_.SetValue("LABEL", GetEnumValUse(enum_def, ev));
        code_.SetValue("TYPE", GetUnionElement(ev, true, opts_));
        code_ += "    case {{LABEL}}: {";
        bool copyable = true;
        if (ev.union_type.base_type == BASE_TYPE_STRUCT &&
            !ev.union_type.struct_def->fixed) {
          // Don't generate code to copy if table is not copyable.
          // TODO(wvo): make tables copyable instead.
          for (auto fit = ev.union_type.struct_def->fields.vec.begin();
               fit != ev.union_type.struct_def->fields.vec.end(); ++fit) {
            const auto &field = **fit;
            if (!field.deprecated && field.value.type.struct_def &&
                !field.native_inline) {
              copyable = false;
              break;
            }
          }
        }
        if (copyable) {
          code_ +=
              "      value = new {{TYPE}}(*reinterpret_cast<{{TYPE}} *>"
              "(u.value));";
        } else {
          code_ +=
              "      FLATBUFFERS_ASSERT(false);  // {{TYPE}} not copyable.";
        }
        code_ += "      break;";
        code_ += "    }";
      }
      code_ += "    default:";
      code_ += "      break;";
      code_ += "  }";
      code_ += "}";
      code_ += "";

      // Union Reset() function.
      FLATBUFFERS_ASSERT(enum_def.Lookup("NONE"));
      code_.SetValue("NONE", GetEnumValUse(enum_def, *enum_def.Lookup("NONE")));

      code_ += "inline void {{ENUM_NAME}}Union::Reset() {";
      code_ += "  switch (type) {";
      for (auto it = enum_def.Vals().begin(); it != enum_def.Vals().end();
           ++it) {
        const auto &ev = **it;
        if (ev.IsZero()) { continue; }
        code_.SetValue("LABEL", GetEnumValUse(enum_def, ev));
        code_.SetValue("TYPE", GetUnionElement(ev, true, opts_));
        code_ += "    case {{LABEL}}: {";
        code_ += "      auto ptr = reinterpret_cast<{{TYPE}} *>(value);";
        code_ += "      delete ptr;";
        code_ += "      break;";
        code_ += "    }";
      }
      code_ += "    default: break;";
      code_ += "  }";
      code_ += "  value = nullptr;";
      code_ += "  type = {{NONE}};";
      code_ += "}";
      code_ += "";
    }
  }

  // Generates a value with optionally a cast applied if the field has a
  // different underlying type from its interface type (currently only the
  // case for enums. "from" specify the direction, true meaning from the
  // underlying type to the interface type.
  std::string GenUnderlyingCast(const FieldDef &field, bool from,
                                const std::string &val) {
    if (from && field.value.type.base_type == BASE_TYPE_BOOL) {
      return val + " != 0";
    } else if ((field.value.type.enum_def &&
                IsScalar(field.value.type.base_type)) ||
               field.value.type.base_type == BASE_TYPE_BOOL) {
      return "static_cast<" + GenTypeBasic(field.value.type, from) + ">(" +
             val + ")";
    } else {
      return val;
    }
  }

  std::string GenFieldOffsetName(const FieldDef &field) {
    std::string uname = Name(field);
    std::transform(uname.begin(), uname.end(), uname.begin(), CharToUpper);
    return "VT_" + uname;
  }

  void GenFullyQualifiedNameGetter(const StructDef &struct_def,
                                   const std::string &name) {
    if (!opts_.generate_name_strings) { return; }
    auto fullname = struct_def.defined_namespace->GetFullyQualifiedName(name);
    code_.SetValue("NAME", fullname);
    code_.SetValue("CONSTEXPR", "FLATBUFFERS_CONSTEXPR");
    code_ += "  static {{CONSTEXPR}} const char *GetFullyQualifiedName() {";
    code_ += "    return \"{{NAME}}\";";
    code_ += "  }";
  }

  std::string GenDefaultConstant(const FieldDef &field) {
    if (IsFloat(field.value.type.base_type))
      return float_const_gen_.GenFloatConstant(field);
    else
      return NumToStringCpp(field.value.constant, field.value.type.base_type);
  }

  std::string GetDefaultScalarValue(const FieldDef &field, bool is_ctor) {
    const auto &type = field.value.type;
    if (field.IsScalarOptional()) {
      return GenOptionalNull();
    } else if (type.enum_def && IsScalar(type.base_type)) {
      auto ev = type.enum_def->FindByValue(field.value.constant);
      if (ev) {
        return WrapInNameSpace(type.enum_def->defined_namespace,
                               GetEnumValUse(*type.enum_def, *ev));
      } else {
        return GenUnderlyingCast(
            field, true, NumToStringCpp(field.value.constant, type.base_type));
      }
    } else if (type.base_type == BASE_TYPE_BOOL) {
      return field.value.constant == "0" ? "false" : "true";
    } else if (field.attributes.Lookup("cpp_type")) {
      if (is_ctor) {
        if (PtrType(&field) == "naked") {
          return "nullptr";
        } else {
          return "";
        }
      } else {
        return "0";
      }
    } else {
      return GenDefaultConstant(field);
    }
  }

  void GenParam(const FieldDef &field, bool direct, const char *prefix) {
    code_.SetValue("PRE", prefix);
    code_.SetValue("PARAM_NAME", Name(field));
    if (direct && IsString(field.value.type)) {
      code_.SetValue("PARAM_TYPE", "const char *");
      code_.SetValue("PARAM_VALUE", "nullptr");
    } else if (direct && IsVector(field.value.type)) {
      const auto vtype = field.value.type.VectorType();
      std::string type;
      if (IsStruct(vtype)) {
        type = WrapInNameSpace(*vtype.struct_def);
      } else {
        type = GenTypeWire(vtype, "", VectorElementUserFacing(vtype));
      }
      if (TypeHasKey(vtype)) {
        code_.SetValue("PARAM_TYPE", "std::vector<" + type + "> *");
      } else {
        code_.SetValue("PARAM_TYPE", "const std::vector<" + type + "> *");
      }
      code_.SetValue("PARAM_VALUE", "nullptr");
    } else {
      const auto &type = field.value.type;
      code_.SetValue("PARAM_VALUE", GetDefaultScalarValue(field, false));
      if (field.IsScalarOptional())
        code_.SetValue("PARAM_TYPE", GenOptionalDecl(type) + " ");
      else
        code_.SetValue("PARAM_TYPE", GenTypeWire(type, " ", true));
    }
    code_ += "{{PRE}}{{PARAM_TYPE}}{{PARAM_NAME}} = {{PARAM_VALUE}}\\";
  }

  // Generate a member, including a default value for scalars and raw pointers.
  void GenMember(const FieldDef &field) {
    if (!field.deprecated &&  // Deprecated fields won't be accessible.
        field.value.type.base_type != BASE_TYPE_UTYPE &&
        (field.value.type.base_type != BASE_TYPE_VECTOR ||
         field.value.type.element != BASE_TYPE_UTYPE)) {
      auto type = GenTypeNative(field.value.type, false, field);
      auto cpp_type = field.attributes.Lookup("cpp_type");
      auto full_type =
          (cpp_type
               ? (IsVector(field.value.type)
                      ? "std::vector<" +
                            GenTypeNativePtr(cpp_type->constant, &field,
                                             false) +
                            "> "
                      : GenTypeNativePtr(cpp_type->constant, &field, false))
               : type + " ");
      // Generate default member initializers for >= C++11.
      std::string field_di = "";
      if (opts_.g_cpp_std >= cpp::CPP_STD_11) {
        field_di = "{}";
        auto native_default = field.attributes.Lookup("native_default");
        // Scalar types get parsed defaults, raw pointers get nullptrs.
        if (IsScalar(field.value.type.base_type)) {
          field_di =
              " = " + (native_default ? std::string(native_default->constant)
                                      : GetDefaultScalarValue(field, true));
        } else if (field.value.type.base_type == BASE_TYPE_STRUCT) {
          if (IsStruct(field.value.type) && native_default) {
            field_di = " = " + native_default->constant;
          }
        }
      }
      code_.SetValue("FIELD_TYPE", full_type);
      code_.SetValue("FIELD_NAME", Name(field));
      code_.SetValue("FIELD_DI", field_di);
      code_ += "  {{FIELD_TYPE}}{{FIELD_NAME}}{{FIELD_DI}};";
    }
  }

  // Generate the default constructor for this struct. Properly initialize all
  // scalar members with default values.
  void GenDefaultConstructor(const StructDef &struct_def) {
    code_.SetValue("NATIVE_NAME",
                   NativeName(Name(struct_def), &struct_def, opts_));
    // In >= C++11, default member initializers are generated.
    if (opts_.g_cpp_std >= cpp::CPP_STD_11) { return; }
    std::string initializer_list;
    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end(); ++it) {
      const auto &field = **it;
      if (!field.deprecated &&  // Deprecated fields won't be accessible.
          field.value.type.base_type != BASE_TYPE_UTYPE) {
        auto cpp_type = field.attributes.Lookup("cpp_type");
        auto native_default = field.attributes.Lookup("native_default");
        // Scalar types get parsed defaults, raw pointers get nullptrs.
        if (IsScalar(field.value.type.base_type)) {
          if (!initializer_list.empty()) { initializer_list += ",\n        "; }
          initializer_list += Name(field);
          initializer_list +=
              "(" +
              (native_default ? std::string(native_default->constant)
                              : GetDefaultScalarValue(field, true)) +
              ")";
        } else if (field.value.type.base_type == BASE_TYPE_STRUCT) {
          if (IsStruct(field.value.type)) {
            if (native_default) {
              if (!initializer_list.empty()) {
                initializer_list += ",\n        ";
              }
              initializer_list +=
                  Name(field) + "(" + native_default->constant + ")";
            }
          }
        } else if (cpp_type && field.value.type.base_type != BASE_TYPE_VECTOR) {
          if (!initializer_list.empty()) { initializer_list += ",\n        "; }
          initializer_list += Name(field) + "(0)";
        }
      }
    }
    if (!initializer_list.empty()) {
      initializer_list = "\n      : " + initializer_list;
    }

    code_.SetValue("INIT_LIST", initializer_list);

    code_ += "  {{NATIVE_NAME}}(){{INIT_LIST}} {";
    code_ += "  }";
  }

  void GenCompareOperator(const StructDef &struct_def,
                          std::string accessSuffix = "") {
    std::string compare_op;
    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end(); ++it) {
      const auto &field = **it;
      if (!field.deprecated &&  // Deprecated fields won't be accessible.
          field.value.type.base_type != BASE_TYPE_UTYPE &&
          (field.value.type.base_type != BASE_TYPE_VECTOR ||
           field.value.type.element != BASE_TYPE_UTYPE)) {
        if (!compare_op.empty()) { compare_op += " &&\n      "; }
        auto accessor = Name(field) + accessSuffix;
        compare_op += "(lhs." + accessor + " == rhs." + accessor + ")";
      }
    }

    std::string cmp_lhs;
    std::string cmp_rhs;
    if (compare_op.empty()) {
      cmp_lhs = "";
      cmp_rhs = "";
      compare_op = "  return true;";
    } else {
      cmp_lhs = "lhs";
      cmp_rhs = "rhs";
      compare_op = "  return\n      " + compare_op + ";";
    }

    code_.SetValue("CMP_OP", compare_op);
    code_.SetValue("CMP_LHS", cmp_lhs);
    code_.SetValue("CMP_RHS", cmp_rhs);
    code_ += "";
    code_ +=
        "inline bool operator==(const {{NATIVE_NAME}} &{{CMP_LHS}}, const "
        "{{NATIVE_NAME}} &{{CMP_RHS}}) {";
    code_ += "{{CMP_OP}}";
    code_ += "}";

    code_ += "";
    code_ +=
        "inline bool operator!=(const {{NATIVE_NAME}} &lhs, const "
        "{{NATIVE_NAME}} &rhs) {";
    code_ += "    return !(lhs == rhs);";
    code_ += "}";
    code_ += "";
  }

  void GenOperatorNewDelete(const StructDef &struct_def) {
    if (auto native_custom_alloc =
            struct_def.attributes.Lookup("native_custom_alloc")) {
      code_ += "  inline void *operator new (std::size_t count) {";
      code_ += "    return " + native_custom_alloc->constant +
               "<{{NATIVE_NAME}}>().allocate(count / sizeof({{NATIVE_NAME}}));";
      code_ += "  }";
      code_ += "  inline void operator delete (void *ptr) {";
      code_ += "    return " + native_custom_alloc->constant +
               "<{{NATIVE_NAME}}>().deallocate(static_cast<{{NATIVE_NAME}}*>("
               "ptr),1);";
      code_ += "  }";
    }
  }

  void GenNativeTable(const StructDef &struct_def) {
    const auto native_name = NativeName(Name(struct_def), &struct_def, opts_);
    code_.SetValue("STRUCT_NAME", Name(struct_def));
    code_.SetValue("NATIVE_NAME", native_name);

    // Generate a C++ object that can hold an unpacked version of this table.
    code_ += "struct {{NATIVE_NAME}} : public flatbuffers::NativeTable {";
    code_ += "  typedef {{STRUCT_NAME}} TableType;";
    GenFullyQualifiedNameGetter(struct_def, native_name);
    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end(); ++it) {
      GenMember(**it);
    }
    GenOperatorNewDelete(struct_def);
    GenDefaultConstructor(struct_def);
    code_ += "};";
    if (opts_.gen_compare) GenCompareOperator(struct_def);
    code_ += "";
  }

  // Generate the code to call the appropriate Verify function(s) for a field.
  void GenVerifyCall(const FieldDef &field, const char *prefix) {
    code_.SetValue("PRE", prefix);
    code_.SetValue("NAME", Name(field));
    code_.SetValue("REQUIRED", field.IsRequired() ? "Required" : "");
    code_.SetValue("SIZE", GenTypeSize(field.value.type));
    code_.SetValue("OFFSET", GenFieldOffsetName(field));
    if (IsScalar(field.value.type.base_type) || IsStruct(field.value.type)) {
      code_ +=
          "{{PRE}}VerifyField{{REQUIRED}}<{{SIZE}}>(verifier, {{OFFSET}})\\";
    } else {
      code_ += "{{PRE}}VerifyOffset{{REQUIRED}}(verifier, {{OFFSET}})\\";
    }

    switch (field.value.type.base_type) {
      case BASE_TYPE_UNION: {
        code_.SetValue("ENUM_NAME", field.value.type.enum_def->name);
        code_.SetValue("SUFFIX", UnionTypeFieldSuffix());
        code_ +=
            "{{PRE}}Verify{{ENUM_NAME}}(verifier, {{NAME}}(), "
            "{{NAME}}{{SUFFIX}}())\\";
        break;
      }
      case BASE_TYPE_STRUCT: {
        if (!field.value.type.struct_def->fixed) {
          code_ += "{{PRE}}verifier.VerifyTable({{NAME}}())\\";
        }
        break;
      }
      case BASE_TYPE_STRING: {
        code_ += "{{PRE}}verifier.VerifyString({{NAME}}())\\";
        break;
      }
      case BASE_TYPE_VECTOR: {
        code_ += "{{PRE}}verifier.VerifyVector({{NAME}}())\\";

        switch (field.value.type.element) {
          case BASE_TYPE_STRING: {
            code_ += "{{PRE}}verifier.VerifyVectorOfStrings({{NAME}}())\\";
            break;
          }
          case BASE_TYPE_STRUCT: {
            if (!field.value.type.struct_def->fixed) {
              code_ += "{{PRE}}verifier.VerifyVectorOfTables({{NAME}}())\\";
            }
            break;
          }
          case BASE_TYPE_UNION: {
            code_.SetValue("ENUM_NAME", field.value.type.enum_def->name);
            code_ +=
                "{{PRE}}Verify{{ENUM_NAME}}Vector(verifier, {{NAME}}(), "
                "{{NAME}}_type())\\";
            break;
          }
          default: break;
        }
        break;
      }
      default: {
        break;
      }
    }
  }

  // Generate CompareWithValue method for a key field.
  void GenKeyFieldMethods(const FieldDef &field) {
    FLATBUFFERS_ASSERT(field.key);
    const bool is_string = (IsString(field.value.type));

    code_ += "  bool KeyCompareLessThan(const {{STRUCT_NAME}} *o) const {";
    if (is_string) {
      // use operator< of flatbuffers::String
      code_ += "    return *{{FIELD_NAME}}() < *o->{{FIELD_NAME}}();";
    } else {
      code_ += "    return {{FIELD_NAME}}() < o->{{FIELD_NAME}}();";
    }
    code_ += "  }";

    if (is_string) {
      code_ += "  int KeyCompareWithValue(const char *val) const {";
      code_ += "    return strcmp({{FIELD_NAME}}()->c_str(), val);";
      code_ += "  }";
    } else {
      FLATBUFFERS_ASSERT(IsScalar(field.value.type.base_type));
      auto type = GenTypeBasic(field.value.type, false);
      if (opts_.scoped_enums && field.value.type.enum_def &&
          IsScalar(field.value.type.base_type)) {
        type = GenTypeGet(field.value.type, " ", "const ", " *", true);
      }
      // Returns {field<val: -1, field==val: 0, field>val: +1}.
      code_.SetValue("KEY_TYPE", type);
      code_ += "  int KeyCompareWithValue({{KEY_TYPE}} val) const {";
      code_ +=
          "    return static_cast<int>({{FIELD_NAME}}() > val) - "
          "static_cast<int>({{FIELD_NAME}}() < val);";
      code_ += "  }";
    }
  }

  void GenTableUnionAsGetters(const FieldDef &field) {
    const auto &type = field.value.type;
    auto u = type.enum_def;

    if (!type.enum_def->uses_multiple_type_instances)
      code_ +=
          "  template<typename T> "
          "const T *{{NULLABLE_EXT}}{{FIELD_NAME}}_as() const;";

    for (auto u_it = u->Vals().begin(); u_it != u->Vals().end(); ++u_it) {
      auto &ev = **u_it;
      if (ev.union_type.base_type == BASE_TYPE_NONE) { continue; }
      auto full_struct_name = GetUnionElement(ev, false, opts_);

      // @TODO: Mby make this decisions more universal? How?
      code_.SetValue("U_GET_TYPE",
                     EscapeKeyword(field.name + UnionTypeFieldSuffix()));
      code_.SetValue("U_ELEMENT_TYPE", WrapInNameSpace(u->defined_namespace,
                                                       GetEnumValUse(*u, ev)));
      code_.SetValue("U_FIELD_TYPE", "const " + full_struct_name + " *");
      code_.SetValue("U_FIELD_NAME", Name(field) + "_as_" + Name(ev));
      code_.SetValue("U_NULLABLE", NullableExtension());

      // `const Type *union_name_asType() const` accessor.
      code_ += "  {{U_FIELD_TYPE}}{{U_NULLABLE}}{{U_FIELD_NAME}}() const {";
      code_ +=
          "    return {{U_GET_TYPE}}() == {{U_ELEMENT_TYPE}} ? "
          "static_cast<{{U_FIELD_TYPE}}>({{FIELD_NAME}}()) "
          ": nullptr;";
      code_ += "  }";
    }
  }

  void GenTableFieldGetter(const FieldDef &field) {
    const auto &type = field.value.type;
    const auto offset_str = GenFieldOffsetName(field);

    GenComment(field.doc_comment, "  ");
    // Call a different accessor for pointers, that indirects.
    if (false == field.IsScalarOptional()) {
      const bool is_scalar = IsScalar(type.base_type);
      std::string accessor;
      if (is_scalar)
        accessor = "GetField<";
      else if (IsStruct(type))
        accessor = "GetStruct<";
      else
        accessor = "GetPointer<";
      auto offset_type = GenTypeGet(type, "", "const ", " *", false);
      auto call = accessor + offset_type + ">(" + offset_str;
      // Default value as second arg for non-pointer types.
      if (is_scalar) { call += ", " + GenDefaultConstant(field); }
      call += ")";

      std::string afterptr = " *" + NullableExtension();
      code_.SetValue("FIELD_TYPE",
                     GenTypeGet(type, " ", "const ", afterptr.c_str(), true));
      code_.SetValue("FIELD_VALUE", GenUnderlyingCast(field, true, call));
      code_.SetValue("NULLABLE_EXT", NullableExtension());
      code_ += "  {{FIELD_TYPE}}{{FIELD_NAME}}() const {";
      code_ += "    return {{FIELD_VALUE}};";
      code_ += "  }";
    } else {
      auto wire_type = GenTypeBasic(type, false);
      auto face_type = GenTypeBasic(type, true);
      auto opt_value = "GetOptional<" + wire_type + ", " + face_type + ">(" +
                       offset_str + ")";
      code_.SetValue("FIELD_TYPE", GenOptionalDecl(type));
      code_ += "  {{FIELD_TYPE}} {{FIELD_NAME}}() const {";
      code_ += "    return " + opt_value + ";";
      code_ += "  }";
    }

    if (type.base_type == BASE_TYPE_UNION) { GenTableUnionAsGetters(field); }
  }

  void GenTableFieldType(const FieldDef &field) {
    const auto &type = field.value.type;
    const auto offset_str = GenFieldOffsetName(field);
    if (!field.IsScalarOptional()) {
      std::string afterptr = " *" + NullableExtension();
      code_.SetValue("FIELD_TYPE",
                     GenTypeGet(type, "", "const ", afterptr.c_str(), true));
      code_ += "    {{FIELD_TYPE}}\\";
    } else {
      code_.SetValue("FIELD_TYPE", GenOptionalDecl(type));
      code_ += "    {{FIELD_TYPE}}\\";
    }
  }

  void GenStructFieldType(const FieldDef &field) {
    const auto is_array = IsArray(field.value.type);
    std::string field_type =
        GenTypeGet(field.value.type, "", is_array ? "" : "const ",
                   is_array ? "" : " &", true);
    code_.SetValue("FIELD_TYPE", field_type);
    code_ += "    {{FIELD_TYPE}}\\";
  }

  void GenFieldTypeHelper(const StructDef &struct_def) {
    if (struct_def.fields.vec.empty()) { return; }
    code_ += "  template<size_t Index>";
    code_ += "  using FieldType = \\";
    code_ += "decltype(std::declval<type>().get_field<Index>());";
  }

  void GenIndexBasedFieldGetter(const StructDef &struct_def) {
    if (struct_def.fields.vec.empty()) { return; }
    code_ += "  template<size_t Index>";
    code_ += "  auto get_field() const {";

    size_t index = 0;
    bool need_else = false;
    // Generate one index-based getter for each field.
    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end(); ++it) {
      const auto &field = **it;
      if (field.deprecated) {
        // Deprecated fields won't be accessible.
        continue;
      }
      code_.SetValue("FIELD_NAME", Name(field));
      code_.SetValue("FIELD_INDEX",
                     std::to_string(static_cast<long long>(index++)));
      if (need_else) {
        code_ += "    else \\";
      } else {
        code_ += "         \\";
      }
      need_else = true;
      code_ += "if constexpr (Index == {{FIELD_INDEX}}) \\";
      code_ += "return {{FIELD_NAME}}();";
    }
    code_ += "    else static_assert(Index != Index, \"Invalid Field Index\");";
    code_ += "  }";
  }

  // Sample for Vec3:
  //
  //   static constexpr std::array<const char *, 3> field_names = {
  //     "x",
  //     "y",
  //     "z"
  //   };
  //
  void GenFieldNames(const StructDef &struct_def) {
    auto non_deprecated_field_count = std::count_if(
        struct_def.fields.vec.begin(), struct_def.fields.vec.end(),
        [](const FieldDef *field) { return !field->deprecated; });
    code_ += "  static constexpr std::array<\\";
    code_.SetValue(
        "FIELD_COUNT",
        std::to_string(static_cast<long long>(non_deprecated_field_count)));
    code_ += "const char *, {{FIELD_COUNT}}> field_names = {\\";
    if (struct_def.fields.vec.empty()) {
      code_ += "};";
      return;
    }
    code_ += "";
    // Generate the field_names elements.
    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end(); ++it) {
      const auto &field = **it;
      if (field.deprecated) {
        // Deprecated fields won't be accessible.
        continue;
      }
      code_.SetValue("FIELD_NAME", Name(field));
      code_ += "    \"{{FIELD_NAME}}\"\\";
      if (it + 1 != struct_def.fields.vec.end()) { code_ += ","; }
    }
    code_ += "\n  };";
  }

  void GenFieldsNumber(const StructDef &struct_def) {
    auto non_deprecated_field_count = std::count_if(
        struct_def.fields.vec.begin(), struct_def.fields.vec.end(),
        [](const FieldDef *field) { return !field->deprecated; });
    code_.SetValue(
        "FIELD_COUNT",
        std::to_string(static_cast<long long>(non_deprecated_field_count)));
    code_ += "  static constexpr size_t fields_number = {{FIELD_COUNT}};";
  }

  void GenTraitsStruct(const StructDef &struct_def) {
    code_.SetValue(
        "FULLY_QUALIFIED_NAME",
        struct_def.defined_namespace->GetFullyQualifiedName(Name(struct_def)));
    code_ += "struct {{STRUCT_NAME}}::Traits {";
    code_ += "  using type = {{STRUCT_NAME}};";
    if (!struct_def.fixed) {
      // We have a table and not a struct.
      code_ += "  static auto constexpr Create = Create{{STRUCT_NAME}};";
    }
    if (opts_.cpp_static_reflection) {
      code_ += "  static constexpr auto name = \"{{STRUCT_NAME}}\";";
      code_ +=
          "  static constexpr auto fully_qualified_name = "
          "\"{{FULLY_QUALIFIED_NAME}}\";";
      GenFieldNames(struct_def);
      GenFieldTypeHelper(struct_def);
      GenFieldsNumber(struct_def);
    }
    code_ += "};";
    code_ += "";
  }

  void GenTableFieldSetter(const FieldDef &field) {
    const auto &type = field.value.type;
    const bool is_scalar = IsScalar(type.base_type);
    if (is_scalar && IsUnion(type))
      return;  // changing of a union's type is forbidden

    auto offset_str = GenFieldOffsetName(field);
    if (is_scalar) {
      const auto wire_type = GenTypeWire(type, "", false);
      code_.SetValue("SET_FN", "SetField<" + wire_type + ">");
      code_.SetValue("OFFSET_NAME", offset_str);
      code_.SetValue("FIELD_TYPE", GenTypeBasic(type, true));
      code_.SetValue("FIELD_VALUE",
                     GenUnderlyingCast(field, false, "_" + Name(field)));

      code_ +=
          "  bool mutate_{{FIELD_NAME}}({{FIELD_TYPE}} "
          "_{{FIELD_NAME}}) {";
      if (false == field.IsScalarOptional()) {
        code_.SetValue("DEFAULT_VALUE", GenDefaultConstant(field));
        code_ +=
            "    return {{SET_FN}}({{OFFSET_NAME}}, {{FIELD_VALUE}}, "
            "{{DEFAULT_VALUE}});";
      } else {
        code_ += "    return {{SET_FN}}({{OFFSET_NAME}}, {{FIELD_VALUE}});";
      }
      code_ += "  }";
    } else {
      auto postptr = " *" + NullableExtension();
      auto wire_type = GenTypeGet(type, " ", "", postptr.c_str(), true);
      std::string accessor = IsStruct(type) ? "GetStruct<" : "GetPointer<";
      auto underlying = accessor + wire_type + ">(" + offset_str + ")";
      code_.SetValue("FIELD_TYPE", wire_type);
      code_.SetValue("FIELD_VALUE", GenUnderlyingCast(field, true, underlying));

      code_ += "  {{FIELD_TYPE}}mutable_{{FIELD_NAME}}() {";
      code_ += "    return {{FIELD_VALUE}};";
      code_ += "  }";
    }
  }

  // Generate an accessor struct, builder structs & function for a table.
  void GenTable(const StructDef &struct_def) {
    if (opts_.generate_object_based_api) { GenNativeTable(struct_def); }

    // Generate an accessor struct, with methods of the form:
    // type name() const { return GetField<type>(offset, defaultval); }
    GenComment(struct_def.doc_comment);

    code_.SetValue("STRUCT_NAME", Name(struct_def));
    code_ +=
        "struct {{STRUCT_NAME}} FLATBUFFERS_FINAL_CLASS"
        " : private flatbuffers::Table {";
    if (opts_.generate_object_based_api) {
      code_ += "  typedef {{NATIVE_NAME}} NativeTableType;";
    }
    code_ += "  typedef {{STRUCT_NAME}}Builder Builder;";
    if (opts_.g_cpp_std >= cpp::CPP_STD_17) { code_ += "  struct Traits;"; }
    if (opts_.mini_reflect != IDLOptions::kNone) {
      code_ +=
          "  static const flatbuffers::TypeTable *MiniReflectTypeTable() {";
      code_ += "    return {{STRUCT_NAME}}TypeTable();";
      code_ += "  }";
    }

    GenFullyQualifiedNameGetter(struct_def, Name(struct_def));

    // Generate field id constants.
    if (struct_def.fields.vec.size() > 0) {
      // We need to add a trailing comma to all elements except the last one as
      // older versions of gcc complain about this.
      code_.SetValue("SEP", "");
      code_ +=
          "  enum FlatBuffersVTableOffset FLATBUFFERS_VTABLE_UNDERLYING_TYPE {";
      for (auto it = struct_def.fields.vec.begin();
           it != struct_def.fields.vec.end(); ++it) {
        const auto &field = **it;
        if (field.deprecated) {
          // Deprecated fields won't be accessible.
          continue;
        }

        code_.SetValue("OFFSET_NAME", GenFieldOffsetName(field));
        code_.SetValue("OFFSET_VALUE", NumToString(field.value.offset));
        code_ += "{{SEP}}    {{OFFSET_NAME}} = {{OFFSET_VALUE}}\\";
        code_.SetValue("SEP", ",\n");
      }
      code_ += "";
      code_ += "  };";
    }

    // Generate the accessors.
    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end(); ++it) {
      const auto &field = **it;
      if (field.deprecated) {
        // Deprecated fields won't be accessible.
        continue;
      }

      code_.SetValue("FIELD_NAME", Name(field));
      GenTableFieldGetter(field);
      if (opts_.mutable_buffer) { GenTableFieldSetter(field); }

      auto nested = field.attributes.Lookup("nested_flatbuffer");
      if (nested) {
        std::string qualified_name = nested->constant;
        auto nested_root = parser_.LookupStruct(nested->constant);
        if (nested_root == nullptr) {
          qualified_name = parser_.current_namespace_->GetFullyQualifiedName(
              nested->constant);
          nested_root = parser_.LookupStruct(qualified_name);
        }
        FLATBUFFERS_ASSERT(nested_root);  // Guaranteed to exist by parser.
        (void)nested_root;
        code_.SetValue("CPP_NAME", TranslateNameSpace(qualified_name));

        code_ += "  const {{CPP_NAME}} *{{FIELD_NAME}}_nested_root() const {";
        code_ +=
            "    return "
            "flatbuffers::GetRoot<{{CPP_NAME}}>({{FIELD_NAME}}()->Data());";
        code_ += "  }";
      }

      if (field.flexbuffer) {
        code_ +=
            "  flexbuffers::Reference {{FIELD_NAME}}_flexbuffer_root()"
            " const {";
        // Both Data() and size() are const-methods, therefore call order
        // doesn't matter.
        code_ +=
            "    return flexbuffers::GetRoot({{FIELD_NAME}}()->Data(), "
            "{{FIELD_NAME}}()->size());";
        code_ += "  }";
      }

      // Generate a comparison function for this field if it is a key.
      if (field.key) { GenKeyFieldMethods(field); }
    }

    if (opts_.cpp_static_reflection) { GenIndexBasedFieldGetter(struct_def); }

    // Generate a verifier function that can check a buffer from an untrusted
    // source will never cause reads outside the buffer.
    code_ += "  bool Verify(flatbuffers::Verifier &verifier) const {";
    code_ += "    return VerifyTableStart(verifier)\\";
    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end(); ++it) {
      const auto &field = **it;
      if (field.deprecated) { continue; }
      GenVerifyCall(field, " &&\n           ");
    }

    code_ += " &&\n           verifier.EndTable();";
    code_ += "  }";

    if (opts_.generate_object_based_api) {
      // Generate the UnPack() pre declaration.
      code_ += "  " + TableUnPackSignature(struct_def, true, opts_) + ";";
      code_ += "  " + TableUnPackToSignature(struct_def, true, opts_) + ";";
      code_ += "  " + TablePackSignature(struct_def, true, opts_) + ";";
    }

    code_ += "};";  // End of table.
    code_ += "";

    // Explicit specializations for union accessors
    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end(); ++it) {
      const auto &field = **it;
      if (field.deprecated || field.value.type.base_type != BASE_TYPE_UNION) {
        continue;
      }

      auto u = field.value.type.enum_def;
      if (u->uses_multiple_type_instances) continue;

      code_.SetValue("FIELD_NAME", Name(field));

      for (auto u_it = u->Vals().begin(); u_it != u->Vals().end(); ++u_it) {
        auto &ev = **u_it;
        if (ev.union_type.base_type == BASE_TYPE_NONE) { continue; }

        auto full_struct_name = GetUnionElement(ev, false, opts_);

        code_.SetValue(
            "U_ELEMENT_TYPE",
            WrapInNameSpace(u->defined_namespace, GetEnumValUse(*u, ev)));
        code_.SetValue("U_FIELD_TYPE", "const " + full_struct_name + " *");
        code_.SetValue("U_ELEMENT_NAME", full_struct_name);
        code_.SetValue("U_FIELD_NAME", Name(field) + "_as_" + Name(ev));

        // `template<> const T *union_name_as<T>() const` accessor.
        code_ +=
            "template<> "
            "inline {{U_FIELD_TYPE}}{{STRUCT_NAME}}::{{FIELD_NAME}}_as"
            "<{{U_ELEMENT_NAME}}>() const {";
        code_ += "  return {{U_FIELD_NAME}}();";
        code_ += "}";
        code_ += "";
      }
    }

    GenBuilders(struct_def);

    if (opts_.generate_object_based_api) {
      // Generate a pre-declaration for a CreateX method that works with an
      // unpacked C++ object.
      code_ += TableCreateSignature(struct_def, true, opts_) + ";";
      code_ += "";
    }
  }

  // Generate code to force vector alignment. Return empty string for vector
  // that doesn't need alignment code.
  std::string GenVectorForceAlign(const FieldDef &field,
                                  const std::string &field_size) {
    FLATBUFFERS_ASSERT(IsVector(field.value.type));
    // Get the value of the force_align attribute.
    const auto *force_align = field.attributes.Lookup("force_align");
    const int align = force_align ? atoi(force_align->constant.c_str()) : 1;
    // Generate code to do force_align for the vector.
    if (align > 1) {
      const auto vtype = field.value.type.VectorType();
      const auto type = IsStruct(vtype) ? WrapInNameSpace(*vtype.struct_def)
                                        : GenTypeWire(vtype, "", false);
      return "_fbb.ForceVectorAlignment(" + field_size + ", sizeof(" + type +
             "), " + std::to_string(static_cast<long long>(align)) + ");";
    }
    return "";
  }

  void GenBuilders(const StructDef &struct_def) {
    code_.SetValue("STRUCT_NAME", Name(struct_def));

    // Generate a builder struct:
    code_ += "struct {{STRUCT_NAME}}Builder {";
    code_ += "  typedef {{STRUCT_NAME}} Table;";
    code_ += "  flatbuffers::FlatBufferBuilder &fbb_;";
    code_ += "  flatbuffers::uoffset_t start_;";

    bool has_string_or_vector_fields = false;
    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end(); ++it) {
      const auto &field = **it;
      if (field.deprecated) continue;
      const bool is_scalar = IsScalar(field.value.type.base_type);
      const bool is_default_scalar = is_scalar && !field.IsScalarOptional();
      const bool is_string = IsString(field.value.type);
      const bool is_vector = IsVector(field.value.type);
      if (is_string || is_vector) { has_string_or_vector_fields = true; }

      std::string offset = GenFieldOffsetName(field);
      std::string name = GenUnderlyingCast(field, false, Name(field));
      std::string value = is_default_scalar ? GenDefaultConstant(field) : "";

      // Generate accessor functions of the form:
      // void add_name(type name) {
      //   fbb_.AddElement<type>(offset, name, default);
      // }
      code_.SetValue("FIELD_NAME", Name(field));
      code_.SetValue("FIELD_TYPE", GenTypeWire(field.value.type, " ", true));
      code_.SetValue("ADD_OFFSET", Name(struct_def) + "::" + offset);
      code_.SetValue("ADD_NAME", name);
      code_.SetValue("ADD_VALUE", value);
      if (is_scalar) {
        const auto type = GenTypeWire(field.value.type, "", false);
        code_.SetValue("ADD_FN", "AddElement<" + type + ">");
      } else if (IsStruct(field.value.type)) {
        code_.SetValue("ADD_FN", "AddStruct");
      } else {
        code_.SetValue("ADD_FN", "AddOffset");
      }

      code_ += "  void add_{{FIELD_NAME}}({{FIELD_TYPE}}{{FIELD_NAME}}) {";
      code_ += "    fbb_.{{ADD_FN}}(\\";
      if (is_default_scalar) {
        code_ += "{{ADD_OFFSET}}, {{ADD_NAME}}, {{ADD_VALUE}});";
      } else {
        code_ += "{{ADD_OFFSET}}, {{ADD_NAME}});";
      }
      code_ += "  }";
    }

    // Builder constructor
    code_ +=
        "  explicit {{STRUCT_NAME}}Builder(flatbuffers::FlatBufferBuilder "
        "&_fbb)";
    code_ += "        : fbb_(_fbb) {";
    code_ += "    start_ = fbb_.StartTable();";
    code_ += "  }";

    // Finish() function.
    code_ += "  flatbuffers::Offset<{{STRUCT_NAME}}> Finish() {";
    code_ += "    const auto end = fbb_.EndTable(start_);";
    code_ += "    auto o = flatbuffers::Offset<{{STRUCT_NAME}}>(end);";

    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end(); ++it) {
      const auto &field = **it;
      if (!field.deprecated && field.IsRequired()) {
        code_.SetValue("FIELD_NAME", Name(field));
        code_.SetValue("OFFSET_NAME", GenFieldOffsetName(field));
        code_ += "    fbb_.Required(o, {{STRUCT_NAME}}::{{OFFSET_NAME}});";
      }
    }
    code_ += "    return o;";
    code_ += "  }";
    code_ += "};";
    code_ += "";

    // Generate a convenient CreateX function that uses the above builder
    // to create a table in one go.
    code_ +=
        "inline flatbuffers::Offset<{{STRUCT_NAME}}> "
        "Create{{STRUCT_NAME}}(";
    code_ += "    flatbuffers::FlatBufferBuilder &_fbb\\";
    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end(); ++it) {
      const auto &field = **it;
      if (!field.deprecated) { GenParam(field, false, ",\n    "); }
    }
    code_ += ") {";

    code_ += "  {{STRUCT_NAME}}Builder builder_(_fbb);";
    for (size_t size = struct_def.sortbysize ? sizeof(largest_scalar_t) : 1;
         size; size /= 2) {
      for (auto it = struct_def.fields.vec.rbegin();
           it != struct_def.fields.vec.rend(); ++it) {
        const auto &field = **it;
        if (!field.deprecated && (!struct_def.sortbysize ||
                                  size == SizeOf(field.value.type.base_type))) {
          code_.SetValue("FIELD_NAME", Name(field));
          if (field.IsScalarOptional()) {
            code_ +=
                "  if({{FIELD_NAME}}) { "
                "builder_.add_{{FIELD_NAME}}(*{{FIELD_NAME}}); }";
          } else {
            code_ += "  builder_.add_{{FIELD_NAME}}({{FIELD_NAME}});";
          }
        }
      }
    }
    code_ += "  return builder_.Finish();";
    code_ += "}";
    code_ += "";

    // Definition for type traits for this table type. This allows querying var-
    // ious compile-time traits of the table.
    if (opts_.g_cpp_std >= cpp::CPP_STD_17) { GenTraitsStruct(struct_def); }

    // Generate a CreateXDirect function with vector types as parameters
    if (opts_.cpp_direct_copy && has_string_or_vector_fields) {
      code_ +=
          "inline flatbuffers::Offset<{{STRUCT_NAME}}> "
          "Create{{STRUCT_NAME}}Direct(";
      code_ += "    flatbuffers::FlatBufferBuilder &_fbb\\";
      for (auto it = struct_def.fields.vec.begin();
           it != struct_def.fields.vec.end(); ++it) {
        const auto &field = **it;
        if (!field.deprecated) { GenParam(field, true, ",\n    "); }
      }
      // Need to call "Create" with the struct namespace.
      const auto qualified_create_name =
          struct_def.defined_namespace->GetFullyQualifiedName("Create");
      code_.SetValue("CREATE_NAME", TranslateNameSpace(qualified_create_name));
      code_ += ") {";
      for (auto it = struct_def.fields.vec.begin();
           it != struct_def.fields.vec.end(); ++it) {
        const auto &field = **it;
        if (!field.deprecated) {
          code_.SetValue("FIELD_NAME", Name(field));
          if (IsString(field.value.type)) {
            if (!field.shared) {
              code_.SetValue("CREATE_STRING", "CreateString");
            } else {
              code_.SetValue("CREATE_STRING", "CreateSharedString");
            }
            code_ +=
                "  auto {{FIELD_NAME}}__ = {{FIELD_NAME}} ? "
                "_fbb.{{CREATE_STRING}}({{FIELD_NAME}}) : 0;";
          } else if (IsVector(field.value.type)) {
            const std::string force_align_code =
                GenVectorForceAlign(field, Name(field) + "->size()");
            if (!force_align_code.empty()) {
              code_ += "  if ({{FIELD_NAME}}) { " + force_align_code + " }";
            }
            code_ += "  auto {{FIELD_NAME}}__ = {{FIELD_NAME}} ? \\";
            const auto vtype = field.value.type.VectorType();
            const auto has_key = TypeHasKey(vtype);
            if (IsStruct(vtype)) {
              const auto type = WrapInNameSpace(*vtype.struct_def);
              code_ += (has_key ? "_fbb.CreateVectorOfSortedStructs<"
                                : "_fbb.CreateVectorOfStructs<") +
                       type + ">\\";
            } else if (has_key) {
              const auto type = WrapInNameSpace(*vtype.struct_def);
              code_ += "_fbb.CreateVectorOfSortedTables<" + type + ">\\";
            } else {
              const auto type =
                  GenTypeWire(vtype, "", VectorElementUserFacing(vtype));
              code_ += "_fbb.CreateVector<" + type + ">\\";
            }
            code_ +=
                has_key ? "({{FIELD_NAME}}) : 0;" : "(*{{FIELD_NAME}}) : 0;";
          }
        }
      }
      code_ += "  return {{CREATE_NAME}}{{STRUCT_NAME}}(";
      code_ += "      _fbb\\";
      for (auto it = struct_def.fields.vec.begin();
           it != struct_def.fields.vec.end(); ++it) {
        const auto &field = **it;
        if (!field.deprecated) {
          code_.SetValue("FIELD_NAME", Name(field));
          code_ += ",\n      {{FIELD_NAME}}\\";
          if (IsString(field.value.type) || IsVector(field.value.type)) {
            code_ += "__\\";
          }
        }
      }
      code_ += ");";
      code_ += "}";
      code_ += "";
    }
  }

  std::string GenUnionUnpackVal(const FieldDef &afield,
                                const char *vec_elem_access,
                                const char *vec_type_access) {
    auto type_name = WrapInNameSpace(*afield.value.type.enum_def);
    return type_name + "Union::UnPack(" + "_e" + vec_elem_access + ", " +
           EscapeKeyword(afield.name + UnionTypeFieldSuffix()) + "()" +
           vec_type_access + ", _resolver)";
  }

  std::string GenUnpackVal(const Type &type, const std::string &val,
                           bool invector, const FieldDef &afield) {
    switch (type.base_type) {
      case BASE_TYPE_STRING: {
        if (FlexibleStringConstructor(&afield)) {
          return NativeString(&afield) + "(" + val + "->c_str(), " + val +
                 "->size())";
        } else {
          return val + "->str()";
        }
      }
      case BASE_TYPE_STRUCT: {
        if (IsStruct(type)) {
          const auto &struct_attrs = type.struct_def->attributes;
          const auto native_type = struct_attrs.Lookup("native_type");
          if (native_type) {
            std::string unpack_call = "flatbuffers::UnPack";
            const auto pack_name = struct_attrs.Lookup("native_type_pack_name");
            if (pack_name) { unpack_call += pack_name->constant; }
            unpack_call += "(*" + val + ")";
            return unpack_call;
          } else if (invector || afield.native_inline) {
            return "*" + val;
          } else {
            const auto name = WrapInNameSpace(*type.struct_def);
            const auto ptype = GenTypeNativePtr(name, &afield, true);
            return ptype + "(new " + name + "(*" + val + "))";
          }
        } else {
          const auto ptype = GenTypeNativePtr(
              WrapNativeNameInNameSpace(*type.struct_def, opts_), &afield,
              true);
          return ptype + "(" + val + "->UnPack(_resolver))";
        }
      }
      case BASE_TYPE_UNION: {
        return GenUnionUnpackVal(
            afield, invector ? "->Get(_i)" : "",
            invector ? ("->GetEnum<" + type.enum_def->name + ">(_i)").c_str()
                     : "");
      }
      default: {
        return val;
        break;
      }
    }
  }

  std::string GenUnpackFieldStatement(const FieldDef &field,
                                      const FieldDef *union_field) {
    std::string code;
    switch (field.value.type.base_type) {
      case BASE_TYPE_VECTOR: {
        auto name = Name(field);
        if (field.value.type.element == BASE_TYPE_UTYPE) {
          name = StripUnionType(Name(field));
        }
        code += "{ _o->" + name + ".resize(_e->size()); ";
        if (!field.value.type.enum_def && !IsBool(field.value.type.element) &&
            IsOneByte(field.value.type.element)) {
          // For vectors of bytes, std::copy is used to improve performance.
          // This doesn't work for:
          //  - enum types because they have to be explicitly static_cast.
          //  - vectors of bool, since they are a template specialization.
          //  - multiple-byte types due to endianness.
          code +=
              "std::copy(_e->begin(), _e->end(), _o->" + name + ".begin()); }";
        } else {
          std::string indexing;
          if (field.value.type.enum_def) {
            indexing += "static_cast<" +
                        WrapInNameSpace(*field.value.type.enum_def) + ">(";
          }
          indexing += "_e->Get(_i)";
          if (field.value.type.enum_def) { indexing += ")"; }
          if (field.value.type.element == BASE_TYPE_BOOL) {
            indexing += " != 0";
          }
          // Generate code that pushes data from _e to _o in the form:
          //   for (uoffset_t i = 0; i < _e->size(); ++i) {
          //     _o->field.push_back(_e->Get(_i));
          //   }
          auto access =
              field.value.type.element == BASE_TYPE_UTYPE
                  ? ".type"
                  : (field.value.type.element == BASE_TYPE_UNION ? ".value"
                                                                 : "");

          code += "for (flatbuffers::uoffset_t _i = 0;";
          code += " _i < _e->size(); _i++) { ";
          auto cpp_type = field.attributes.Lookup("cpp_type");
          if (cpp_type) {
            // Generate code that resolves the cpp pointer type, of the form:
            //  if (resolver)
            //    (*resolver)(&_o->field, (hash_value_t)(_e));
            //  else
            //    _o->field = nullptr;
            code += "//vector resolver, " + PtrType(&field) + "\n";
            code += "if (_resolver) ";
            code += "(*_resolver)";
            code += "(reinterpret_cast<void **>(&_o->" + name + "[_i]" +
                    access + "), ";
            code +=
                "static_cast<flatbuffers::hash_value_t>(" + indexing + "));";
            if (PtrType(&field) == "naked") {
              code += " else ";
              code += "_o->" + name + "[_i]" + access + " = nullptr";
            } else {
              // code += " else ";
              // code += "_o->" + name + "[_i]" + access + " = " +
              // GenTypeNativePtr(cpp_type->constant, &field, true) + "();";
              code += "/* else do nothing */";
            }
          } else {
            code += "_o->" + name + "[_i]" + access + " = ";
            code += GenUnpackVal(field.value.type.VectorType(), indexing, true,
                                 field);
          }
          code += "; } }";
        }
        break;
      }
      case BASE_TYPE_UTYPE: {
        FLATBUFFERS_ASSERT(union_field->value.type.base_type ==
                           BASE_TYPE_UNION);
        // Generate code that sets the union type, of the form:
        //   _o->field.type = _e;
        code += "_o->" + union_field->name + ".type = _e;";
        break;
      }
      case BASE_TYPE_UNION: {
        // Generate code that sets the union value, of the form:
        //   _o->field.value = Union::Unpack(_e, field_type(), resolver);
        code += "_o->" + Name(field) + ".value = ";
        code += GenUnionUnpackVal(field, "", "");
        code += ";";
        break;
      }
      default: {
        auto cpp_type = field.attributes.Lookup("cpp_type");
        if (cpp_type) {
          // Generate code that resolves the cpp pointer type, of the form:
          //  if (resolver)
          //    (*resolver)(&_o->field, (hash_value_t)(_e));
          //  else
          //    _o->field = nullptr;
          code += "//scalar resolver, " + PtrType(&field) + " \n";
          code += "if (_resolver) ";
          code += "(*_resolver)";
          code += "(reinterpret_cast<void **>(&_o->" + Name(field) + "), ";
          code += "static_cast<flatbuffers::hash_value_t>(_e));";
          if (PtrType(&field) == "naked") {
            code += " else ";
            code += "_o->" + Name(field) + " = nullptr;";
          } else {
            // code += " else ";
            // code += "_o->" + Name(field) + " = " +
            // GenTypeNativePtr(cpp_type->constant, &field, true) + "();";
            code += "/* else do nothing */;";
          }
        } else {
          // Generate code for assigning the value, of the form:
          //  _o->field = value;
          code += "_o->" + Name(field) + " = ";
          code += GenUnpackVal(field.value.type, "_e", false, field) + ";";
        }
        break;
      }
    }
    return code;
  }

  std::string GenCreateParam(const FieldDef &field) {
    std::string value = "_o->";
    if (field.value.type.base_type == BASE_TYPE_UTYPE) {
      value += StripUnionType(Name(field));
      value += ".type";
    } else {
      value += Name(field);
    }
    if (field.value.type.base_type != BASE_TYPE_VECTOR &&
        field.attributes.Lookup("cpp_type")) {
      auto type = GenTypeBasic(field.value.type, false);
      value =
          "_rehasher ? "
          "static_cast<" +
          type + ">((*_rehasher)(" + value + GenPtrGet(field) + ")) : 0";
    }

    std::string code;
    switch (field.value.type.base_type) {
      // String fields are of the form:
      //   _fbb.CreateString(_o->field)
      // or
      //   _fbb.CreateSharedString(_o->field)
      case BASE_TYPE_STRING: {
        if (!field.shared) {
          code += "_fbb.CreateString(";
        } else {
          code += "_fbb.CreateSharedString(";
        }
        code += value;
        code.push_back(')');

        // For optional fields, check to see if there actually is any data
        // in _o->field before attempting to access it. If there isn't,
        // depending on set_empty_strings_to_null either set it to 0 or an empty
        // string.
        if (!field.IsRequired()) {
          auto empty_value = opts_.set_empty_strings_to_null
                                 ? "0"
                                 : "_fbb.CreateSharedString(\"\")";
          code = value + ".empty() ? " + empty_value + " : " + code;
        }
        break;
      }
        // Vector fields come in several flavours, of the forms:
        //   _fbb.CreateVector(_o->field);
        //   _fbb.CreateVector((const utype*)_o->field.data(),
        //   _o->field.size()); _fbb.CreateVectorOfStrings(_o->field)
        //   _fbb.CreateVectorOfStructs(_o->field)
        //   _fbb.CreateVector<Offset<T>>(_o->field.size() [&](size_t i) {
        //     return CreateT(_fbb, _o->Get(i), rehasher);
        //   });
      case BASE_TYPE_VECTOR: {
        auto vector_type = field.value.type.VectorType();
        switch (vector_type.base_type) {
          case BASE_TYPE_STRING: {
            if (NativeString(&field) == "std::string") {
              code += "_fbb.CreateVectorOfStrings(" + value + ")";
            } else {
              // Use by-function serialization to emulate
              // CreateVectorOfStrings(); this works also with non-std strings.
              code +=
                  "_fbb.CreateVector<flatbuffers::Offset<flatbuffers::String>>"
                  " ";
              code += "(" + value + ".size(), ";
              code += "[](size_t i, _VectorArgs *__va) { ";
              code +=
                  "return __va->__fbb->CreateString(__va->_" + value + "[i]);";
              code += " }, &_va )";
            }
            break;
          }
          case BASE_TYPE_STRUCT: {
            if (IsStruct(vector_type)) {
              const auto &struct_attrs =
                  field.value.type.struct_def->attributes;
              const auto native_type = struct_attrs.Lookup("native_type");
              if (native_type) {
                code += "_fbb.CreateVectorOfNativeStructs<";
                code += WrapInNameSpace(*vector_type.struct_def) + ", " +
                        native_type->constant + ">";
                code += "(" + value;
                const auto pack_name =
                    struct_attrs.Lookup("native_type_pack_name");
                if (pack_name) {
                  code += ", flatbuffers::Pack" + pack_name->constant;
                }
                code += ")";
              } else {
                code += "_fbb.CreateVectorOfStructs";
                code += "(" + value + ")";
              }
            } else {
              code += "_fbb.CreateVector<flatbuffers::Offset<";
              code += WrapInNameSpace(*vector_type.struct_def) + ">> ";
              code += "(" + value + ".size(), ";
              code += "[](size_t i, _VectorArgs *__va) { ";
              code += "return Create" + vector_type.struct_def->name;
              code += "(*__va->__fbb, __va->_" + value + "[i]" +
                      GenPtrGet(field) + ", ";
              code += "__va->__rehasher); }, &_va )";
            }
            break;
          }
          case BASE_TYPE_BOOL: {
            code += "_fbb.CreateVector(" + value + ")";
            break;
          }
          case BASE_TYPE_UNION: {
            code +=
                "_fbb.CreateVector<flatbuffers::"
                "Offset<void>>(" +
                value +
                ".size(), [](size_t i, _VectorArgs *__va) { "
                "return __va->_" +
                value + "[i].Pack(*__va->__fbb, __va->__rehasher); }, &_va)";
            break;
          }
          case BASE_TYPE_UTYPE: {
            value = StripUnionType(value);
            code += "_fbb.CreateVector<uint8_t>(" + value +
                    ".size(), [](size_t i, _VectorArgs *__va) { "
                    "return static_cast<uint8_t>(__va->_" +
                    value + "[i].type); }, &_va)";
            break;
          }
          default: {
            if (field.value.type.enum_def &&
                !VectorElementUserFacing(vector_type)) {
              // For enumerations, we need to get access to the array data for
              // the underlying storage type (eg. uint8_t).
              const auto basetype = GenTypeBasic(
                  field.value.type.enum_def->underlying_type, false);
              code += "_fbb.CreateVectorScalarCast<" + basetype +
                      ">(flatbuffers::data(" + value + "), " + value +
                      ".size())";
            } else if (field.attributes.Lookup("cpp_type")) {
              auto type = GenTypeBasic(vector_type, false);
              code += "_fbb.CreateVector<" + type + ">(" + value + ".size(), ";
              code += "[](size_t i, _VectorArgs *__va) { ";
              code += "return __va->__rehasher ? ";
              code += "static_cast<" + type + ">((*__va->__rehasher)";
              code += "(__va->_" + value + "[i]" + GenPtrGet(field) + ")) : 0";
              code += "; }, &_va )";
            } else {
              code += "_fbb.CreateVector(" + value + ")";
            }
            break;
          }
        }

        // If set_empty_vectors_to_null option is enabled, for optional fields,
        // check to see if there actually is any data in _o->field before
        // attempting to access it.
        if (opts_.set_empty_vectors_to_null && !field.IsRequired()) {
          code = value + ".size() ? " + code + " : 0";
        }
        break;
      }
      case BASE_TYPE_UNION: {
        // _o->field.Pack(_fbb);
        code += value + ".Pack(_fbb)";
        break;
      }
      case BASE_TYPE_STRUCT: {
        if (IsStruct(field.value.type)) {
          const auto &struct_attribs = field.value.type.struct_def->attributes;
          const auto native_type = struct_attribs.Lookup("native_type");
          if (native_type) {
            code += "flatbuffers::Pack";
            const auto pack_name =
                struct_attribs.Lookup("native_type_pack_name");
            if (pack_name) { code += pack_name->constant; }
            code += "(" + value + ")";
          } else if (field.native_inline) {
            code += "&" + value;
          } else {
            code += value + " ? " + value + GenPtrGet(field) + " : 0";
          }
        } else {
          // _o->field ? CreateT(_fbb, _o->field.get(), _rehasher);
          const auto type = field.value.type.struct_def->name;
          code += value + " ? Create" + type;
          code += "(_fbb, " + value + GenPtrGet(field) + ", _rehasher)";
          code += " : 0";
        }
        break;
      }
      default: {
        code += value;
        break;
      }
    }
    return code;
  }

  // Generate code for tables that needs to come after the regular definition.
  void GenTablePost(const StructDef &struct_def) {
    code_.SetValue("STRUCT_NAME", Name(struct_def));
    code_.SetValue("NATIVE_NAME",
                   NativeName(Name(struct_def), &struct_def, opts_));

    if (opts_.generate_object_based_api) {
      // Generate the X::UnPack() method.
      code_ +=
          "inline " + TableUnPackSignature(struct_def, false, opts_) + " {";

      if (opts_.g_cpp_std == cpp::CPP_STD_X0) {
        auto native_name = WrapNativeNameInNameSpace(struct_def, parser_.opts);
        code_.SetValue("POINTER_TYPE",
                       GenTypeNativePtr(native_name, nullptr, false));
        code_ +=
            "  {{POINTER_TYPE}} _o = {{POINTER_TYPE}}(new {{NATIVE_NAME}}());";
      } else if (opts_.g_cpp_std == cpp::CPP_STD_11) {
        code_ +=
            "  auto _o = std::unique_ptr<{{NATIVE_NAME}}>(new "
            "{{NATIVE_NAME}}());";
      } else {
        code_ += "  auto _o = std::make_unique<{{NATIVE_NAME}}>();";
      }
      code_ += "  UnPackTo(_o.get(), _resolver);";
      code_ += "  return _o.release();";
      code_ += "}";
      code_ += "";
      code_ +=
          "inline " + TableUnPackToSignature(struct_def, false, opts_) + " {";
      code_ += "  (void)_o;";
      code_ += "  (void)_resolver;";

      for (auto it = struct_def.fields.vec.begin();
           it != struct_def.fields.vec.end(); ++it) {
        const auto &field = **it;
        if (field.deprecated) { continue; }

        // Assign a value from |this| to |_o|.   Values from |this| are stored
        // in a variable |_e| by calling this->field_type().  The value is then
        // assigned to |_o| using the GenUnpackFieldStatement.
        const bool is_union = field.value.type.base_type == BASE_TYPE_UTYPE;
        const auto statement =
            GenUnpackFieldStatement(field, is_union ? *(it + 1) : nullptr);

        code_.SetValue("FIELD_NAME", Name(field));
        auto prefix = "  { auto _e = {{FIELD_NAME}}(); ";
        auto check = IsScalar(field.value.type.base_type) ? "" : "if (_e) ";
        auto postfix = " }";
        code_ += std::string(prefix) + check + statement + postfix;
      }
      code_ += "}";
      code_ += "";

      // Generate the X::Pack member function that simply calls the global
      // CreateX function.
      code_ += "inline " + TablePackSignature(struct_def, false, opts_) + " {";
      code_ += "  return Create{{STRUCT_NAME}}(_fbb, _o, _rehasher);";
      code_ += "}";
      code_ += "";

      // Generate a CreateX method that works with an unpacked C++ object.
      code_ +=
          "inline " + TableCreateSignature(struct_def, false, opts_) + " {";
      code_ += "  (void)_rehasher;";
      code_ += "  (void)_o;";

      code_ +=
          "  struct _VectorArgs "
          "{ flatbuffers::FlatBufferBuilder *__fbb; "
          "const " +
          NativeName(Name(struct_def), &struct_def, opts_) +
          "* __o; "
          "const flatbuffers::rehasher_function_t *__rehasher; } _va = { "
          "&_fbb, _o, _rehasher}; (void)_va;";

      for (auto it = struct_def.fields.vec.begin();
           it != struct_def.fields.vec.end(); ++it) {
        auto &field = **it;
        if (field.deprecated) { continue; }
        if (IsVector(field.value.type)) {
          const std::string force_align_code =
              GenVectorForceAlign(field, "_o->" + Name(field) + ".size()");
          if (!force_align_code.empty()) { code_ += "  " + force_align_code; }
        }
        code_ += "  auto _" + Name(field) + " = " + GenCreateParam(field) + ";";
      }
      // Need to call "Create" with the struct namespace.
      const auto qualified_create_name =
          struct_def.defined_namespace->GetFullyQualifiedName("Create");
      code_.SetValue("CREATE_NAME", TranslateNameSpace(qualified_create_name));

      code_ += "  return {{CREATE_NAME}}{{STRUCT_NAME}}(";
      code_ += "      _fbb\\";
      for (auto it = struct_def.fields.vec.begin();
           it != struct_def.fields.vec.end(); ++it) {
        auto &field = **it;
        if (field.deprecated) { continue; }

        bool pass_by_address = false;
        if (field.value.type.base_type == BASE_TYPE_STRUCT) {
          if (IsStruct(field.value.type)) {
            auto native_type =
                field.value.type.struct_def->attributes.Lookup("native_type");
            if (native_type) { pass_by_address = true; }
          }
        }

        // Call the CreateX function using values from |_o|.
        if (pass_by_address) {
          code_ += ",\n      &_" + Name(field) + "\\";
        } else {
          code_ += ",\n      _" + Name(field) + "\\";
        }
      }
      code_ += ");";
      code_ += "}";
      code_ += "";
    }
  }

  static void GenPadding(
      const FieldDef &field, std::string *code_ptr, int *id,
      const std::function<void(int bits, std::string *code_ptr, int *id)> &f) {
    if (field.padding) {
      for (int i = 0; i < 4; i++) {
        if (static_cast<int>(field.padding) & (1 << i)) {
          f((1 << i) * 8, code_ptr, id);
        }
      }
      FLATBUFFERS_ASSERT(!(field.padding & ~0xF));
    }
  }

  static void PaddingDefinition(int bits, std::string *code_ptr, int *id) {
    *code_ptr += "  int" + NumToString(bits) + "_t padding" +
                 NumToString((*id)++) + "__;";
  }

  static void PaddingInitializer(int bits, std::string *code_ptr, int *id) {
    (void)bits;
    if (!code_ptr->empty()) *code_ptr += ",\n        ";
    *code_ptr += "padding" + NumToString((*id)++) + "__(0)";
  }

  static void PaddingNoop(int bits, std::string *code_ptr, int *id) {
    (void)bits;
    if (!code_ptr->empty()) *code_ptr += '\n';
    *code_ptr += "    (void)padding" + NumToString((*id)++) + "__;";
  }

  void GenStructDefaultConstructor(const StructDef &struct_def) {
    std::string init_list;
    std::string body;
    bool first_in_init_list = true;
    int padding_initializer_id = 0;
    int padding_body_id = 0;
    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end(); ++it) {
      const auto field = *it;
      const auto field_name = field->name + "_";

      if (first_in_init_list) {
        first_in_init_list = false;
      } else {
        init_list += ",";
        init_list += "\n        ";
      }

      init_list += field_name;
      if (IsStruct(field->value.type) || IsArray(field->value.type)) {
        // this is either default initialization of struct
        // or
        // implicit initialization of array
        // for each object in array it:
        // * sets it as zeros for POD types (integral, floating point, etc)
        // * calls default constructor for classes/structs
        init_list += "()";
      } else {
        init_list += "(0)";
      }
      if (field->padding) {
        GenPadding(*field, &init_list, &padding_initializer_id,
                   PaddingInitializer);
        GenPadding(*field, &body, &padding_body_id, PaddingNoop);
      }
    }

    if (init_list.empty()) {
      code_ += "  {{STRUCT_NAME}}()";
      code_ += "  {}";
    } else {
      code_.SetValue("INIT_LIST", init_list);
      code_ += "  {{STRUCT_NAME}}()";
      code_ += "      : {{INIT_LIST}} {";
      if (!body.empty()) { code_ += body; }
      code_ += "  }";
    }
  }

  void GenStructConstructor(const StructDef &struct_def,
                            GenArrayArgMode array_mode) {
    std::string arg_list;
    std::string init_list;
    int padding_id = 0;
    auto first = struct_def.fields.vec.begin();
    // skip arrays if generate ctor without array assignment
    const auto init_arrays = (array_mode != kArrayArgModeNone);
    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end(); ++it) {
      const auto &field = **it;
      const auto &type = field.value.type;
      const auto is_array = IsArray(type);
      const auto arg_name = "_" + Name(field);
      if (!is_array || init_arrays) {
        if (it != first && !arg_list.empty()) { arg_list += ", "; }
        arg_list += !is_array ? GenTypeGet(type, " ", "const ", " &", true)
                              : GenTypeSpan(type, true, type.fixed_length);
        arg_list += arg_name;
      }
      // skip an array with initialization from span
      if (false == (is_array && init_arrays)) {
        if (it != first && !init_list.empty()) { init_list += ",\n        "; }
        init_list += Name(field) + "_";
        if (IsScalar(type.base_type)) {
          auto scalar_type = GenUnderlyingCast(field, false, arg_name);
          init_list += "(flatbuffers::EndianScalar(" + scalar_type + "))";
        } else {
          FLATBUFFERS_ASSERT((is_array && !init_arrays) || IsStruct(type));
          if (!is_array)
            init_list += "(" + arg_name + ")";
          else
            init_list += "()";
        }
      }
      if (field.padding)
        GenPadding(field, &init_list, &padding_id, PaddingInitializer);
    }

    if (!arg_list.empty()) {
      code_.SetValue("ARG_LIST", arg_list);
      code_.SetValue("INIT_LIST", init_list);
      if (!init_list.empty()) {
        code_ += "  {{STRUCT_NAME}}({{ARG_LIST}})";
        code_ += "      : {{INIT_LIST}} {";
      } else {
        code_ += "  {{STRUCT_NAME}}({{ARG_LIST}}) {";
      }
      padding_id = 0;
      for (auto it = struct_def.fields.vec.begin();
           it != struct_def.fields.vec.end(); ++it) {
        const auto &field = **it;
        const auto &type = field.value.type;
        if (IsArray(type) && init_arrays) {
          const auto &element_type = type.VectorType();
          const auto is_enum = IsEnum(element_type);
          FLATBUFFERS_ASSERT(
              (IsScalar(element_type.base_type) || IsStruct(element_type)) &&
              "invalid declaration");
          const auto face_type = GenTypeGet(type, " ", "", "", is_enum);
          std::string get_array =
              is_enum ? "CastToArrayOfEnum<" + face_type + ">" : "CastToArray";
          const auto field_name = Name(field) + "_";
          const auto arg_name = "_" + Name(field);
          code_ += "    flatbuffers::" + get_array + "(" + field_name +
                   ").CopyFromSpan(" + arg_name + ");";
        }
        if (field.padding) {
          std::string padding;
          GenPadding(field, &padding, &padding_id, PaddingNoop);
          code_ += padding;
        }
      }
      code_ += "  }";
    }
  }

  void GenArrayAccessor(const Type &type, bool mutable_accessor) {
    FLATBUFFERS_ASSERT(IsArray(type));
    const auto is_enum = IsEnum(type.VectorType());
    // The Array<bool,N> is a tricky case, like std::vector<bool>.
    // It requires a specialization of Array class.
    // Generate Array<uint8_t> for Array<bool>.
    const auto face_type = GenTypeGet(type, " ", "", "", is_enum);
    std::string ret_type = "flatbuffers::Array<" + face_type + ", " +
                           NumToString(type.fixed_length) + ">";
    if (mutable_accessor)
      code_ += "  " + ret_type + " *mutable_{{FIELD_NAME}}() {";
    else
      code_ += "  const " + ret_type + " *{{FIELD_NAME}}() const {";

    std::string get_array =
        is_enum ? "CastToArrayOfEnum<" + face_type + ">" : "CastToArray";
    code_ += "    return &flatbuffers::" + get_array + "({{FIELD_VALUE}});";
    code_ += "  }";
  }

  // Generate an accessor struct with constructor for a flatbuffers struct.
  void GenStruct(const StructDef &struct_def) {
    // Generate an accessor struct, with private variables of the form:
    // type name_;
    // Generates manual padding and alignment.
    // Variables are private because they contain little endian data on all
    // platforms.
    GenComment(struct_def.doc_comment);
    code_.SetValue("ALIGN", NumToString(struct_def.minalign));
    code_.SetValue("STRUCT_NAME", Name(struct_def));

    code_ +=
        "FLATBUFFERS_MANUALLY_ALIGNED_STRUCT({{ALIGN}}) "
        "{{STRUCT_NAME}} FLATBUFFERS_FINAL_CLASS {";
    code_ += " private:";

    int padding_id = 0;
    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end(); ++it) {
      const auto &field = **it;
      const auto &field_type = field.value.type;
      code_.SetValue("FIELD_TYPE", GenTypeGet(field_type, " ", "", " ", false));
      code_.SetValue("FIELD_NAME", Name(field));
      code_.SetValue("ARRAY",
                     IsArray(field_type)
                         ? "[" + NumToString(field_type.fixed_length) + "]"
                         : "");
      code_ += ("  {{FIELD_TYPE}}{{FIELD_NAME}}_{{ARRAY}};");

      if (field.padding) {
        std::string padding;
        GenPadding(field, &padding, &padding_id, PaddingDefinition);
        code_ += padding;
      }
    }

    // Generate GetFullyQualifiedName
    code_ += "";
    code_ += " public:";

    if (opts_.g_cpp_std >= cpp::CPP_STD_17) { code_ += "  struct Traits;"; }

    // Make TypeTable accessible via the generated struct.
    if (opts_.mini_reflect != IDLOptions::kNone) {
      code_ +=
          "  static const flatbuffers::TypeTable *MiniReflectTypeTable() {";
      code_ += "    return {{STRUCT_NAME}}TypeTable();";
      code_ += "  }";
    }

    GenFullyQualifiedNameGetter(struct_def, Name(struct_def));

    // Generate a default constructor.
    GenStructDefaultConstructor(struct_def);

    // Generate a constructor that takes all fields as arguments,
    // excluding arrays.
    GenStructConstructor(struct_def, kArrayArgModeNone);

    auto arrays_num = std::count_if(struct_def.fields.vec.begin(),
                                    struct_def.fields.vec.end(),
                                    [](const flatbuffers::FieldDef *fd) {
                                      return IsArray(fd->value.type);
                                    });
    if (arrays_num > 0) {
      GenStructConstructor(struct_def, kArrayArgModeSpanStatic);
    }

    // Generate accessor methods of the form:
    // type name() const { return flatbuffers::EndianScalar(name_); }
    for (auto it = struct_def.fields.vec.begin();
         it != struct_def.fields.vec.end(); ++it) {
      const auto &field = **it;
      const auto &type = field.value.type;
      const auto is_scalar = IsScalar(type.base_type);
      const auto is_array = IsArray(type);

      const auto field_type = GenTypeGet(type, " ", is_array ? "" : "const ",
                                         is_array ? "" : " &", true);
      auto member = Name(field) + "_";
      auto value =
          is_scalar ? "flatbuffers::EndianScalar(" + member + ")" : member;

      code_.SetValue("FIELD_NAME", Name(field));
      code_.SetValue("FIELD_TYPE", field_type);
      code_.SetValue("FIELD_VALUE", GenUnderlyingCast(field, true, value));

      GenComment(field.doc_comment, "  ");

      // Generate a const accessor function.
      if (is_array) {
        GenArrayAccessor(type, false);
      } else {
        code_ += "  {{FIELD_TYPE}}{{FIELD_NAME}}() const {";
        code_ += "    return {{FIELD_VALUE}};";
        code_ += "  }";
      }

      // Generate a mutable accessor function.
      if (opts_.mutable_buffer) {
        auto mut_field_type =
            GenTypeGet(type, " ", "", is_array ? "" : " &", true);
        code_.SetValue("FIELD_TYPE", mut_field_type);
        if (is_scalar) {
          code_.SetValue("ARG", GenTypeBasic(type, true));
          code_.SetValue("FIELD_VALUE",
                         GenUnderlyingCast(field, false, "_" + Name(field)));

          code_ += "  void mutate_{{FIELD_NAME}}({{ARG}} _{{FIELD_NAME}}) {";
          code_ +=
              "    flatbuffers::WriteScalar(&{{FIELD_NAME}}_, "
              "{{FIELD_VALUE}});";
          code_ += "  }";
        } else if (is_array) {
          GenArrayAccessor(type, true);
        } else {
          code_ += "  {{FIELD_TYPE}}mutable_{{FIELD_NAME}}() {";
          code_ += "    return {{FIELD_VALUE}};";
          code_ += "  }";
        }
      }

      // Generate a comparison function for this field if it is a key.
      if (field.key) { GenKeyFieldMethods(field); }
    }
    code_.SetValue("NATIVE_NAME", Name(struct_def));
    GenOperatorNewDelete(struct_def);

    if (opts_.cpp_static_reflection) { GenIndexBasedFieldGetter(struct_def); }

    code_ += "};";

    code_.SetValue("STRUCT_BYTE_SIZE", NumToString(struct_def.bytesize));
    code_ += "FLATBUFFERS_STRUCT_END({{STRUCT_NAME}}, {{STRUCT_BYTE_SIZE}});";
    if (opts_.gen_compare) GenCompareOperator(struct_def, "()");
    code_ += "";

    // Definition for type traits for this table type. This allows querying var-
    // ious compile-time traits of the table.
    if (opts_.g_cpp_std >= cpp::CPP_STD_17) { GenTraitsStruct(struct_def); }
  }

  // Set up the correct namespace. Only open a namespace if the existing one is
  // different (closing/opening only what is necessary).
  //
  // The file must start and end with an empty (or null) namespace so that
  // namespaces are properly opened and closed.
  void SetNameSpace(const Namespace *ns) {
    if (cur_name_space_ == ns) { return; }

    // Compute the size of the longest common namespace prefix.
    // If cur_name_space is A::B::C::D and ns is A::B::E::F::G,
    // the common prefix is A::B:: and we have old_size = 4, new_size = 5
    // and common_prefix_size = 2
    size_t old_size = cur_name_space_ ? cur_name_space_->components.size() : 0;
    size_t new_size = ns ? ns->components.size() : 0;

    size_t common_prefix_size = 0;
    while (common_prefix_size < old_size && common_prefix_size < new_size &&
           ns->components[common_prefix_size] ==
               cur_name_space_->components[common_prefix_size]) {
      common_prefix_size++;
    }

    // Close cur_name_space in reverse order to reach the common prefix.
    // In the previous example, D then C are closed.
    for (size_t j = old_size; j > common_prefix_size; --j) {
      code_ += "}  // namespace " + cur_name_space_->components[j - 1];
    }
    if (old_size != common_prefix_size) { code_ += ""; }

    // open namespace parts to reach the ns namespace
    // in the previous example, E, then F, then G are opened
    for (auto j = common_prefix_size; j != new_size; ++j) {
      code_ += "namespace " + ns->components[j] + " {";
    }
    if (new_size != common_prefix_size) { code_ += ""; }

    cur_name_space_ = ns;
  }
};

}  // namespace cpp

bool GenerateCPP(const Parser &parser, const std::string &path,
                 const std::string &file_name) {
  cpp::IDLOptionsCpp opts(parser.opts);
  // The '--cpp_std' argument could be extended (like ASAN):
  // Example: "flatc --cpp_std c++17:option1:option2".
  auto cpp_std = !opts.cpp_std.empty() ? opts.cpp_std : "C++11";
  std::transform(cpp_std.begin(), cpp_std.end(), cpp_std.begin(), CharToUpper);
  if (cpp_std == "C++0X") {
    opts.g_cpp_std = cpp::CPP_STD_X0;
    opts.g_only_fixed_enums = false;
  } else if (cpp_std == "C++11") {
    // Use the standard C++11 code generator.
    opts.g_cpp_std = cpp::CPP_STD_11;
    opts.g_only_fixed_enums = true;
  } else if (cpp_std == "C++17") {
    opts.g_cpp_std = cpp::CPP_STD_17;
    // With c++17 generate strong enums only.
    opts.scoped_enums = true;
    // By default, prefixed_enums==true, reset it.
    opts.prefixed_enums = false;
  } else {
    LogCompilerError("Unknown value of the '--cpp-std' switch: " +
                     opts.cpp_std);
    return false;
  }
  // The opts.scoped_enums has priority.
  opts.g_only_fixed_enums |= opts.scoped_enums;

  if (opts.cpp_static_reflection && opts.g_cpp_std < cpp::CPP_STD_17) {
    LogCompilerError(
        "--cpp-static-reflection requires using --cpp-std at \"C++17\" or "
        "higher.");
    return false;
  }

  cpp::CppGenerator generator(parser, path, file_name, opts);
  return generator.generate();
}

std::string CPPMakeRule(const Parser &parser, const std::string &path,
                        const std::string &file_name) {
  const auto filebase =
      flatbuffers::StripPath(flatbuffers::StripExtension(file_name));
  cpp::CppGenerator geneartor(parser, path, file_name, parser.opts);
  const auto included_files = parser.GetIncludedFilesRecursive(file_name);
  std::string make_rule =
      geneartor.GeneratedFileName(path, filebase, parser.opts) + ": ";
  for (auto it = included_files.begin(); it != included_files.end(); ++it) {
    make_rule += " " + *it;
  }
  return make_rule;
}

}  // namespace flatbuffers
