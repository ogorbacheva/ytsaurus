// Generated by the protocol buffer compiler.  DO NOT EDIT!
// source: google/protobuf/struct.proto

#include <google/protobuf/struct.pb.h>

#include <algorithm>

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/extension_set.h>
#include <google/protobuf/wire_format_lite.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/generated_message_reflection.h>
#include <google/protobuf/reflection_ops.h>
#include <google/protobuf/wire_format.h>
// @@protoc_insertion_point(includes)
#include <google/protobuf/port_def.inc>

PROTOBUF_PRAGMA_INIT_SEG
PROTOBUF_NAMESPACE_OPEN
constexpr Struct_FieldsEntry_DoNotUse::Struct_FieldsEntry_DoNotUse(
  ::PROTOBUF_NAMESPACE_ID::internal::ConstantInitialized){}
struct Struct_FieldsEntry_DoNotUseDefaultTypeInternal {
  constexpr Struct_FieldsEntry_DoNotUseDefaultTypeInternal()
    : _instance(::PROTOBUF_NAMESPACE_ID::internal::ConstantInitialized{}) {}
  ~Struct_FieldsEntry_DoNotUseDefaultTypeInternal() {}
  union {
    Struct_FieldsEntry_DoNotUse _instance;
  };
};
PROTOBUF_ATTRIBUTE_NO_DESTROY PROTOBUF_CONSTINIT Struct_FieldsEntry_DoNotUseDefaultTypeInternal _Struct_FieldsEntry_DoNotUse_default_instance_;
constexpr Struct::Struct(
  ::PROTOBUF_NAMESPACE_ID::internal::ConstantInitialized)
  : fields_(::PROTOBUF_NAMESPACE_ID::internal::ConstantInitialized{}){}
struct StructDefaultTypeInternal {
  constexpr StructDefaultTypeInternal()
    : _instance(::PROTOBUF_NAMESPACE_ID::internal::ConstantInitialized{}) {}
  ~StructDefaultTypeInternal() {}
  union {
    Struct _instance;
  };
};
PROTOBUF_ATTRIBUTE_NO_DESTROY PROTOBUF_CONSTINIT StructDefaultTypeInternal _Struct_default_instance_;
constexpr Value::Value(
  ::PROTOBUF_NAMESPACE_ID::internal::ConstantInitialized)
  : _oneof_case_{}{}
struct ValueDefaultTypeInternal {
  constexpr ValueDefaultTypeInternal()
    : _instance(::PROTOBUF_NAMESPACE_ID::internal::ConstantInitialized{}) {}
  ~ValueDefaultTypeInternal() {}
  union {
    Value _instance;
  };
};
PROTOBUF_ATTRIBUTE_NO_DESTROY PROTOBUF_CONSTINIT ValueDefaultTypeInternal _Value_default_instance_;
constexpr ListValue::ListValue(
  ::PROTOBUF_NAMESPACE_ID::internal::ConstantInitialized)
  : values_(){}
struct ListValueDefaultTypeInternal {
  constexpr ListValueDefaultTypeInternal()
    : _instance(::PROTOBUF_NAMESPACE_ID::internal::ConstantInitialized{}) {}
  ~ListValueDefaultTypeInternal() {}
  union {
    ListValue _instance;
  };
};
PROTOBUF_ATTRIBUTE_NO_DESTROY PROTOBUF_CONSTINIT ListValueDefaultTypeInternal _ListValue_default_instance_;
PROTOBUF_NAMESPACE_CLOSE
static ::PROTOBUF_NAMESPACE_ID::Metadata file_level_metadata_google_2fprotobuf_2fstruct_2eproto[4];
static const ::PROTOBUF_NAMESPACE_ID::EnumDescriptor* file_level_enum_descriptors_google_2fprotobuf_2fstruct_2eproto[1];
static constexpr ::PROTOBUF_NAMESPACE_ID::ServiceDescriptor const** file_level_service_descriptors_google_2fprotobuf_2fstruct_2eproto = nullptr;

const arc_ui32 TableStruct_google_2fprotobuf_2fstruct_2eproto::offsets[] PROTOBUF_SECTION_VARIABLE(protodesc_cold) = {
  PROTOBUF_FIELD_OFFSET(::PROTOBUF_NAMESPACE_ID::Struct_FieldsEntry_DoNotUse, _has_bits_),
  PROTOBUF_FIELD_OFFSET(::PROTOBUF_NAMESPACE_ID::Struct_FieldsEntry_DoNotUse, _internal_metadata_),
  ~0u,  // no _extensions_
  ~0u,  // no _oneof_case_
  ~0u,  // no _weak_field_map_
  ~0u,  // no _inlined_string_donated_
  PROTOBUF_FIELD_OFFSET(::PROTOBUF_NAMESPACE_ID::Struct_FieldsEntry_DoNotUse, key_),
  PROTOBUF_FIELD_OFFSET(::PROTOBUF_NAMESPACE_ID::Struct_FieldsEntry_DoNotUse, value_),
  0,
  1,
  ~0u,  // no _has_bits_
  PROTOBUF_FIELD_OFFSET(::PROTOBUF_NAMESPACE_ID::Struct, _internal_metadata_),
  ~0u,  // no _extensions_
  ~0u,  // no _oneof_case_
  ~0u,  // no _weak_field_map_
  ~0u,  // no _inlined_string_donated_
  PROTOBUF_FIELD_OFFSET(::PROTOBUF_NAMESPACE_ID::Struct, fields_),
  ~0u,  // no _has_bits_
  PROTOBUF_FIELD_OFFSET(::PROTOBUF_NAMESPACE_ID::Value, _internal_metadata_),
  ~0u,  // no _extensions_
  PROTOBUF_FIELD_OFFSET(::PROTOBUF_NAMESPACE_ID::Value, _oneof_case_[0]),
  ~0u,  // no _weak_field_map_
  ~0u,  // no _inlined_string_donated_
  ::PROTOBUF_NAMESPACE_ID::internal::kInvalidFieldOffsetTag,
  ::PROTOBUF_NAMESPACE_ID::internal::kInvalidFieldOffsetTag,
  ::PROTOBUF_NAMESPACE_ID::internal::kInvalidFieldOffsetTag,
  ::PROTOBUF_NAMESPACE_ID::internal::kInvalidFieldOffsetTag,
  ::PROTOBUF_NAMESPACE_ID::internal::kInvalidFieldOffsetTag,
  ::PROTOBUF_NAMESPACE_ID::internal::kInvalidFieldOffsetTag,
  PROTOBUF_FIELD_OFFSET(::PROTOBUF_NAMESPACE_ID::Value, kind_),
  ~0u,  // no _has_bits_
  PROTOBUF_FIELD_OFFSET(::PROTOBUF_NAMESPACE_ID::ListValue, _internal_metadata_),
  ~0u,  // no _extensions_
  ~0u,  // no _oneof_case_
  ~0u,  // no _weak_field_map_
  ~0u,  // no _inlined_string_donated_
  PROTOBUF_FIELD_OFFSET(::PROTOBUF_NAMESPACE_ID::ListValue, values_),
};
static const ::PROTOBUF_NAMESPACE_ID::internal::MigrationSchema schemas[] PROTOBUF_SECTION_VARIABLE(protodesc_cold) = {
  { 0, 8, -1, sizeof(::PROTOBUF_NAMESPACE_ID::Struct_FieldsEntry_DoNotUse)},
  { 10, -1, -1, sizeof(::PROTOBUF_NAMESPACE_ID::Struct)},
  { 17, -1, -1, sizeof(::PROTOBUF_NAMESPACE_ID::Value)},
  { 30, -1, -1, sizeof(::PROTOBUF_NAMESPACE_ID::ListValue)},
};

static ::PROTOBUF_NAMESPACE_ID::Message const * const file_default_instances[] = {
  reinterpret_cast<const ::PROTOBUF_NAMESPACE_ID::Message*>(&::PROTOBUF_NAMESPACE_ID::_Struct_FieldsEntry_DoNotUse_default_instance_),
  reinterpret_cast<const ::PROTOBUF_NAMESPACE_ID::Message*>(&::PROTOBUF_NAMESPACE_ID::_Struct_default_instance_),
  reinterpret_cast<const ::PROTOBUF_NAMESPACE_ID::Message*>(&::PROTOBUF_NAMESPACE_ID::_Value_default_instance_),
  reinterpret_cast<const ::PROTOBUF_NAMESPACE_ID::Message*>(&::PROTOBUF_NAMESPACE_ID::_ListValue_default_instance_),
};

const char descriptor_table_protodef_google_2fprotobuf_2fstruct_2eproto[] PROTOBUF_SECTION_VARIABLE(protodesc_cold) =
  "\n\034google/protobuf/struct.proto\022\017google.p"
  "rotobuf\"\204\001\n\006Struct\0223\n\006fields\030\001 \003(\0132#.goo"
  "gle.protobuf.Struct.FieldsEntry\032E\n\013Field"
  "sEntry\022\013\n\003key\030\001 \001(\t\022%\n\005value\030\002 \001(\0132\026.goo"
  "gle.protobuf.Value:\0028\001\"\352\001\n\005Value\0220\n\nnull"
  "_value\030\001 \001(\0162\032.google.protobuf.NullValue"
  "H\000\022\026\n\014number_value\030\002 \001(\001H\000\022\026\n\014string_val"
  "ue\030\003 \001(\tH\000\022\024\n\nbool_value\030\004 \001(\010H\000\022/\n\014stru"
  "ct_value\030\005 \001(\0132\027.google.protobuf.StructH"
  "\000\0220\n\nlist_value\030\006 \001(\0132\032.google.protobuf."
  "ListValueH\000B\006\n\004kind\"3\n\tListValue\022&\n\006valu"
  "es\030\001 \003(\0132\026.google.protobuf.Value*\033\n\tNull"
  "Value\022\016\n\nNULL_VALUE\020\000B\177\n\023com.google.prot"
  "obufB\013StructProtoP\001Z/google.golang.org/p"
  "rotobuf/types/known/structpb\370\001\001\242\002\003GPB\252\002\036"
  "Google.Protobuf.WellKnownTypesb\006proto3"
  ;
static ::PROTOBUF_NAMESPACE_ID::internal::once_flag descriptor_table_google_2fprotobuf_2fstruct_2eproto_once;
const ::PROTOBUF_NAMESPACE_ID::internal::DescriptorTable descriptor_table_google_2fprotobuf_2fstruct_2eproto = {
  false, false, 638, descriptor_table_protodef_google_2fprotobuf_2fstruct_2eproto, "google/protobuf/struct.proto", 
  &descriptor_table_google_2fprotobuf_2fstruct_2eproto_once, nullptr, 0, 4,
  schemas, file_default_instances, TableStruct_google_2fprotobuf_2fstruct_2eproto::offsets,
  file_level_metadata_google_2fprotobuf_2fstruct_2eproto, file_level_enum_descriptors_google_2fprotobuf_2fstruct_2eproto, file_level_service_descriptors_google_2fprotobuf_2fstruct_2eproto,
};
PROTOBUF_ATTRIBUTE_WEAK const ::PROTOBUF_NAMESPACE_ID::internal::DescriptorTable* descriptor_table_google_2fprotobuf_2fstruct_2eproto_getter() {
  return &descriptor_table_google_2fprotobuf_2fstruct_2eproto;
}

// Force running AddDescriptors() at dynamic initialization time.
PROTOBUF_ATTRIBUTE_INIT_PRIORITY static ::PROTOBUF_NAMESPACE_ID::internal::AddDescriptorsRunner dynamic_init_dummy_google_2fprotobuf_2fstruct_2eproto(&descriptor_table_google_2fprotobuf_2fstruct_2eproto);
PROTOBUF_NAMESPACE_OPEN
const ::PROTOBUF_NAMESPACE_ID::EnumDescriptor* NullValue_descriptor() {
  ::PROTOBUF_NAMESPACE_ID::internal::AssignDescriptors(&descriptor_table_google_2fprotobuf_2fstruct_2eproto);
  return file_level_enum_descriptors_google_2fprotobuf_2fstruct_2eproto[0];
}
bool NullValue_IsValid(int value) {
  switch (value) {
    case 0:
      return true;
    default:
      return false;
  }
}


// ===================================================================

Struct_FieldsEntry_DoNotUse::Struct_FieldsEntry_DoNotUse() {}
Struct_FieldsEntry_DoNotUse::Struct_FieldsEntry_DoNotUse(::PROTOBUF_NAMESPACE_ID::Arena* arena)
    : SuperType(arena) {}
void Struct_FieldsEntry_DoNotUse::MergeFrom(const Struct_FieldsEntry_DoNotUse& other) {
  MergeFromInternal(other);
}
::PROTOBUF_NAMESPACE_ID::Metadata Struct_FieldsEntry_DoNotUse::GetMetadata() const {
  return ::PROTOBUF_NAMESPACE_ID::internal::AssignDescriptors(
      &descriptor_table_google_2fprotobuf_2fstruct_2eproto_getter, &descriptor_table_google_2fprotobuf_2fstruct_2eproto_once,
      file_level_metadata_google_2fprotobuf_2fstruct_2eproto[0]);
}

// ===================================================================

class Struct::_Internal {
 public:
};

Struct::Struct(::PROTOBUF_NAMESPACE_ID::Arena* arena,
                         bool is_message_owned)
  : ::PROTOBUF_NAMESPACE_ID::Message(arena, is_message_owned),
  fields_(arena) {
  SharedCtor();
  if (!is_message_owned) {
    RegisterArenaDtor(arena);
  }
  // @@protoc_insertion_point(arena_constructor:google.protobuf.Struct)
}
Struct::Struct(const Struct& from)
  : ::PROTOBUF_NAMESPACE_ID::Message() {
  _internal_metadata_.MergeFrom<::PROTOBUF_NAMESPACE_ID::UnknownFieldSet>(from._internal_metadata_);
  fields_.MergeFrom(from.fields_);
  // @@protoc_insertion_point(copy_constructor:google.protobuf.Struct)
}

inline void Struct::SharedCtor() {
}

Struct::~Struct() {
  // @@protoc_insertion_point(destructor:google.protobuf.Struct)
  if (GetArenaForAllocation() != nullptr) return;
  SharedDtor();
  _internal_metadata_.Delete<::PROTOBUF_NAMESPACE_ID::UnknownFieldSet>();
}

inline void Struct::SharedDtor() {
  GOOGLE_DCHECK(GetArenaForAllocation() == nullptr);
}

void Struct::ArenaDtor(void* object) {
  Struct* _this = reinterpret_cast< Struct* >(object);
  (void)_this;
  _this->fields_. ~MapField();
}
inline void Struct::RegisterArenaDtor(::PROTOBUF_NAMESPACE_ID::Arena* arena) {
  if (arena != nullptr) {
    arena->OwnCustomDestructor(this, &Struct::ArenaDtor);
  }
}
void Struct::SetCachedSize(int size) const {
  _cached_size_.Set(size);
}

void Struct::Clear() {
// @@protoc_insertion_point(message_clear_start:google.protobuf.Struct)
  arc_ui32 cached_has_bits = 0;
  // Prevent compiler warnings about cached_has_bits being unused
  (void) cached_has_bits;

  fields_.Clear();
  _internal_metadata_.Clear<::PROTOBUF_NAMESPACE_ID::UnknownFieldSet>();
}

const char* Struct::_InternalParse(const char* ptr, ::PROTOBUF_NAMESPACE_ID::internal::ParseContext* ctx) {
#define CHK_(x) if (PROTOBUF_PREDICT_FALSE(!(x))) goto failure
  while (!ctx->Done(&ptr)) {
    arc_ui32 tag;
    ptr = ::PROTOBUF_NAMESPACE_ID::internal::ReadTag(ptr, &tag);
    switch (tag >> 3) {
      // map<string, .google.protobuf.Value> fields = 1;
      case 1:
        if (PROTOBUF_PREDICT_TRUE(static_cast<uint8_t>(tag) == 10)) {
          ptr -= 1;
          do {
            ptr += 1;
            ptr = ctx->ParseMessage(&fields_, ptr);
            CHK_(ptr);
            if (!ctx->DataAvailable(ptr)) break;
          } while (::PROTOBUF_NAMESPACE_ID::internal::ExpectTag<10>(ptr));
        } else
          goto handle_unusual;
        continue;
      default:
        goto handle_unusual;
    }  // switch
  handle_unusual:
    if ((tag == 0) || ((tag & 7) == 4)) {
      CHK_(ptr);
      ctx->SetLastTag(tag);
      goto message_done;
    }
    ptr = UnknownFieldParse(
        tag,
        _internal_metadata_.mutable_unknown_fields<::PROTOBUF_NAMESPACE_ID::UnknownFieldSet>(),
        ptr, ctx);
    CHK_(ptr != nullptr);
  }  // while
message_done:
  return ptr;
failure:
  ptr = nullptr;
  goto message_done;
#undef CHK_
}

uint8_t* Struct::_InternalSerialize(
    uint8_t* target, ::PROTOBUF_NAMESPACE_ID::io::EpsCopyOutputStream* stream) const {
  // @@protoc_insertion_point(serialize_to_array_start:google.protobuf.Struct)
  arc_ui32 cached_has_bits = 0;
  (void) cached_has_bits;

  // map<string, .google.protobuf.Value> fields = 1;
  if (!this->_internal_fields().empty()) {
    typedef ::PROTOBUF_NAMESPACE_ID::Map< TProtoStringType, ::PROTOBUF_NAMESPACE_ID::Value >::const_pointer
        ConstPtr;
    typedef ConstPtr SortItem;
    typedef ::PROTOBUF_NAMESPACE_ID::internal::CompareByDerefFirst<SortItem> Less;
    struct Utf8Check {
      static void Check(ConstPtr p) {
        (void)p;
        ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::VerifyUtf8String(
          p->first.data(), static_cast<int>(p->first.length()),
          ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::SERIALIZE,
          "google.protobuf.Struct.FieldsEntry.key");
      }
    };

    if (stream->IsSerializationDeterministic() &&
        this->_internal_fields().size() > 1) {
      ::std::unique_ptr<SortItem[]> items(
          new SortItem[this->_internal_fields().size()]);
      typedef ::PROTOBUF_NAMESPACE_ID::Map< TProtoStringType, ::PROTOBUF_NAMESPACE_ID::Value >::size_type size_type;
      size_type n = 0;
      for (::PROTOBUF_NAMESPACE_ID::Map< TProtoStringType, ::PROTOBUF_NAMESPACE_ID::Value >::const_iterator
          it = this->_internal_fields().begin();
          it != this->_internal_fields().end(); ++it, ++n) {
        items[static_cast<ptrdiff_t>(n)] = SortItem(&*it);
      }
      ::std::sort(&items[0], &items[static_cast<ptrdiff_t>(n)], Less());
      for (size_type i = 0; i < n; i++) {
        target = Struct_FieldsEntry_DoNotUse::Funcs::InternalSerialize(1, items[static_cast<ptrdiff_t>(i)]->first, items[static_cast<ptrdiff_t>(i)]->second, target, stream);
        Utf8Check::Check(&(*items[static_cast<ptrdiff_t>(i)]));
      }
    } else {
      for (::PROTOBUF_NAMESPACE_ID::Map< TProtoStringType, ::PROTOBUF_NAMESPACE_ID::Value >::const_iterator
          it = this->_internal_fields().begin();
          it != this->_internal_fields().end(); ++it) {
        target = Struct_FieldsEntry_DoNotUse::Funcs::InternalSerialize(1, it->first, it->second, target, stream);
        Utf8Check::Check(&(*it));
      }
    }
  }

  if (PROTOBUF_PREDICT_FALSE(_internal_metadata_.have_unknown_fields())) {
    target = ::PROTOBUF_NAMESPACE_ID::internal::WireFormat::InternalSerializeUnknownFieldsToArray(
        _internal_metadata_.unknown_fields<::PROTOBUF_NAMESPACE_ID::UnknownFieldSet>(::PROTOBUF_NAMESPACE_ID::UnknownFieldSet::default_instance), target, stream);
  }
  // @@protoc_insertion_point(serialize_to_array_end:google.protobuf.Struct)
  return target;
}

size_t Struct::ByteSizeLong() const {
// @@protoc_insertion_point(message_byte_size_start:google.protobuf.Struct)
  size_t total_size = 0;

  arc_ui32 cached_has_bits = 0;
  // Prevent compiler warnings about cached_has_bits being unused
  (void) cached_has_bits;

  // map<string, .google.protobuf.Value> fields = 1;
  total_size += 1 *
      ::PROTOBUF_NAMESPACE_ID::internal::FromIntSize(this->_internal_fields_size());
  for (::PROTOBUF_NAMESPACE_ID::Map< TProtoStringType, ::PROTOBUF_NAMESPACE_ID::Value >::const_iterator
      it = this->_internal_fields().begin();
      it != this->_internal_fields().end(); ++it) {
    total_size += Struct_FieldsEntry_DoNotUse::Funcs::ByteSizeLong(it->first, it->second);
  }

  return MaybeComputeUnknownFieldsSize(total_size, &_cached_size_);
}

const ::PROTOBUF_NAMESPACE_ID::Message::ClassData Struct::_class_data_ = {
    ::PROTOBUF_NAMESPACE_ID::Message::CopyWithSizeCheck,
    Struct::MergeImpl
};
const ::PROTOBUF_NAMESPACE_ID::Message::ClassData*Struct::GetClassData() const { return &_class_data_; }

void Struct::MergeImpl(::PROTOBUF_NAMESPACE_ID::Message* to,
                      const ::PROTOBUF_NAMESPACE_ID::Message& from) {
  static_cast<Struct *>(to)->MergeFrom(
      static_cast<const Struct &>(from));
}


void Struct::MergeFrom(const Struct& from) {
// @@protoc_insertion_point(class_specific_merge_from_start:google.protobuf.Struct)
  GOOGLE_DCHECK_NE(&from, this);
  arc_ui32 cached_has_bits = 0;
  (void) cached_has_bits;

  fields_.MergeFrom(from.fields_);
  _internal_metadata_.MergeFrom<::PROTOBUF_NAMESPACE_ID::UnknownFieldSet>(from._internal_metadata_);
}

void Struct::CopyFrom(const Struct& from) {
// @@protoc_insertion_point(class_specific_copy_from_start:google.protobuf.Struct)
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

bool Struct::IsInitialized() const {
  return true;
}

void Struct::InternalSwap(Struct* other) {
  using std::swap;
  _internal_metadata_.InternalSwap(&other->_internal_metadata_);
  fields_.InternalSwap(&other->fields_);
}

::PROTOBUF_NAMESPACE_ID::Metadata Struct::GetMetadata() const {
  return ::PROTOBUF_NAMESPACE_ID::internal::AssignDescriptors(
      &descriptor_table_google_2fprotobuf_2fstruct_2eproto_getter, &descriptor_table_google_2fprotobuf_2fstruct_2eproto_once,
      file_level_metadata_google_2fprotobuf_2fstruct_2eproto[1]);
}

// ===================================================================

class Value::_Internal {
 public:
  static const ::PROTOBUF_NAMESPACE_ID::Struct& struct_value(const Value* msg);
  static const ::PROTOBUF_NAMESPACE_ID::ListValue& list_value(const Value* msg);
};

const ::PROTOBUF_NAMESPACE_ID::Struct&
Value::_Internal::struct_value(const Value* msg) {
  return *msg->kind_.struct_value_;
}
const ::PROTOBUF_NAMESPACE_ID::ListValue&
Value::_Internal::list_value(const Value* msg) {
  return *msg->kind_.list_value_;
}
void Value::set_allocated_struct_value(::PROTOBUF_NAMESPACE_ID::Struct* struct_value) {
  ::PROTOBUF_NAMESPACE_ID::Arena* message_arena = GetArenaForAllocation();
  clear_kind();
  if (struct_value) {
    ::PROTOBUF_NAMESPACE_ID::Arena* submessage_arena =
      ::PROTOBUF_NAMESPACE_ID::Arena::InternalHelper<::PROTOBUF_NAMESPACE_ID::Struct>::GetOwningArena(struct_value);
    if (message_arena != submessage_arena) {
      struct_value = ::PROTOBUF_NAMESPACE_ID::internal::GetOwnedMessage(
          message_arena, struct_value, submessage_arena);
    }
    set_has_struct_value();
    kind_.struct_value_ = struct_value;
  }
  // @@protoc_insertion_point(field_set_allocated:google.protobuf.Value.struct_value)
}
void Value::set_allocated_list_value(::PROTOBUF_NAMESPACE_ID::ListValue* list_value) {
  ::PROTOBUF_NAMESPACE_ID::Arena* message_arena = GetArenaForAllocation();
  clear_kind();
  if (list_value) {
    ::PROTOBUF_NAMESPACE_ID::Arena* submessage_arena =
      ::PROTOBUF_NAMESPACE_ID::Arena::InternalHelper<::PROTOBUF_NAMESPACE_ID::ListValue>::GetOwningArena(list_value);
    if (message_arena != submessage_arena) {
      list_value = ::PROTOBUF_NAMESPACE_ID::internal::GetOwnedMessage(
          message_arena, list_value, submessage_arena);
    }
    set_has_list_value();
    kind_.list_value_ = list_value;
  }
  // @@protoc_insertion_point(field_set_allocated:google.protobuf.Value.list_value)
}
Value::Value(::PROTOBUF_NAMESPACE_ID::Arena* arena,
                         bool is_message_owned)
  : ::PROTOBUF_NAMESPACE_ID::Message(arena, is_message_owned) {
  SharedCtor();
  if (!is_message_owned) {
    RegisterArenaDtor(arena);
  }
  // @@protoc_insertion_point(arena_constructor:google.protobuf.Value)
}
Value::Value(const Value& from)
  : ::PROTOBUF_NAMESPACE_ID::Message() {
  _internal_metadata_.MergeFrom<::PROTOBUF_NAMESPACE_ID::UnknownFieldSet>(from._internal_metadata_);
  clear_has_kind();
  switch (from.kind_case()) {
    case kNullValue: {
      _internal_set_null_value(from._internal_null_value());
      break;
    }
    case kNumberValue: {
      _internal_set_number_value(from._internal_number_value());
      break;
    }
    case kStringValue: {
      _internal_set_string_value(from._internal_string_value());
      break;
    }
    case kBoolValue: {
      _internal_set_bool_value(from._internal_bool_value());
      break;
    }
    case kStructValue: {
      _internal_mutable_struct_value()->::PROTOBUF_NAMESPACE_ID::Struct::MergeFrom(from._internal_struct_value());
      break;
    }
    case kListValue: {
      _internal_mutable_list_value()->::PROTOBUF_NAMESPACE_ID::ListValue::MergeFrom(from._internal_list_value());
      break;
    }
    case KIND_NOT_SET: {
      break;
    }
  }
  // @@protoc_insertion_point(copy_constructor:google.protobuf.Value)
}

inline void Value::SharedCtor() {
clear_has_kind();
}

Value::~Value() {
  // @@protoc_insertion_point(destructor:google.protobuf.Value)
  if (GetArenaForAllocation() != nullptr) return;
  SharedDtor();
  _internal_metadata_.Delete<::PROTOBUF_NAMESPACE_ID::UnknownFieldSet>();
}

inline void Value::SharedDtor() {
  GOOGLE_DCHECK(GetArenaForAllocation() == nullptr);
  if (has_kind()) {
    clear_kind();
  }
}

void Value::ArenaDtor(void* object) {
  Value* _this = reinterpret_cast< Value* >(object);
  (void)_this;
}
void Value::RegisterArenaDtor(::PROTOBUF_NAMESPACE_ID::Arena*) {
}
void Value::SetCachedSize(int size) const {
  _cached_size_.Set(size);
}

void Value::clear_kind() {
// @@protoc_insertion_point(one_of_clear_start:google.protobuf.Value)
  switch (kind_case()) {
    case kNullValue: {
      // No need to clear
      break;
    }
    case kNumberValue: {
      // No need to clear
      break;
    }
    case kStringValue: {
      kind_.string_value_.Destroy(::PROTOBUF_NAMESPACE_ID::internal::ArenaStringPtr::EmptyDefault{}, GetArenaForAllocation());
      break;
    }
    case kBoolValue: {
      // No need to clear
      break;
    }
    case kStructValue: {
      if (GetArenaForAllocation() == nullptr) {
        delete kind_.struct_value_;
      }
      break;
    }
    case kListValue: {
      if (GetArenaForAllocation() == nullptr) {
        delete kind_.list_value_;
      }
      break;
    }
    case KIND_NOT_SET: {
      break;
    }
  }
  _oneof_case_[0] = KIND_NOT_SET;
}


void Value::Clear() {
// @@protoc_insertion_point(message_clear_start:google.protobuf.Value)
  arc_ui32 cached_has_bits = 0;
  // Prevent compiler warnings about cached_has_bits being unused
  (void) cached_has_bits;

  clear_kind();
  _internal_metadata_.Clear<::PROTOBUF_NAMESPACE_ID::UnknownFieldSet>();
}

const char* Value::_InternalParse(const char* ptr, ::PROTOBUF_NAMESPACE_ID::internal::ParseContext* ctx) {
#define CHK_(x) if (PROTOBUF_PREDICT_FALSE(!(x))) goto failure
  while (!ctx->Done(&ptr)) {
    arc_ui32 tag;
    ptr = ::PROTOBUF_NAMESPACE_ID::internal::ReadTag(ptr, &tag);
    switch (tag >> 3) {
      // .google.protobuf.NullValue null_value = 1;
      case 1:
        if (PROTOBUF_PREDICT_TRUE(static_cast<uint8_t>(tag) == 8)) {
          arc_ui64 val = ::PROTOBUF_NAMESPACE_ID::internal::ReadVarint64(&ptr);
          CHK_(ptr);
          _internal_set_null_value(static_cast<::PROTOBUF_NAMESPACE_ID::NullValue>(val));
        } else
          goto handle_unusual;
        continue;
      // double number_value = 2;
      case 2:
        if (PROTOBUF_PREDICT_TRUE(static_cast<uint8_t>(tag) == 17)) {
          _internal_set_number_value(::PROTOBUF_NAMESPACE_ID::internal::UnalignedLoad<double>(ptr));
          ptr += sizeof(double);
        } else
          goto handle_unusual;
        continue;
      // string string_value = 3;
      case 3:
        if (PROTOBUF_PREDICT_TRUE(static_cast<uint8_t>(tag) == 26)) {
          auto str = _internal_mutable_string_value();
          ptr = ::PROTOBUF_NAMESPACE_ID::internal::InlineGreedyStringParser(str, ptr, ctx);
          CHK_(::PROTOBUF_NAMESPACE_ID::internal::VerifyUTF8(str, "google.protobuf.Value.string_value"));
          CHK_(ptr);
        } else
          goto handle_unusual;
        continue;
      // bool bool_value = 4;
      case 4:
        if (PROTOBUF_PREDICT_TRUE(static_cast<uint8_t>(tag) == 32)) {
          _internal_set_bool_value(::PROTOBUF_NAMESPACE_ID::internal::ReadVarint64(&ptr));
          CHK_(ptr);
        } else
          goto handle_unusual;
        continue;
      // .google.protobuf.Struct struct_value = 5;
      case 5:
        if (PROTOBUF_PREDICT_TRUE(static_cast<uint8_t>(tag) == 42)) {
          ptr = ctx->ParseMessage(_internal_mutable_struct_value(), ptr);
          CHK_(ptr);
        } else
          goto handle_unusual;
        continue;
      // .google.protobuf.ListValue list_value = 6;
      case 6:
        if (PROTOBUF_PREDICT_TRUE(static_cast<uint8_t>(tag) == 50)) {
          ptr = ctx->ParseMessage(_internal_mutable_list_value(), ptr);
          CHK_(ptr);
        } else
          goto handle_unusual;
        continue;
      default:
        goto handle_unusual;
    }  // switch
  handle_unusual:
    if ((tag == 0) || ((tag & 7) == 4)) {
      CHK_(ptr);
      ctx->SetLastTag(tag);
      goto message_done;
    }
    ptr = UnknownFieldParse(
        tag,
        _internal_metadata_.mutable_unknown_fields<::PROTOBUF_NAMESPACE_ID::UnknownFieldSet>(),
        ptr, ctx);
    CHK_(ptr != nullptr);
  }  // while
message_done:
  return ptr;
failure:
  ptr = nullptr;
  goto message_done;
#undef CHK_
}

uint8_t* Value::_InternalSerialize(
    uint8_t* target, ::PROTOBUF_NAMESPACE_ID::io::EpsCopyOutputStream* stream) const {
  // @@protoc_insertion_point(serialize_to_array_start:google.protobuf.Value)
  arc_ui32 cached_has_bits = 0;
  (void) cached_has_bits;

  // .google.protobuf.NullValue null_value = 1;
  if (_internal_has_null_value()) {
    target = stream->EnsureSpace(target);
    target = ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::WriteEnumToArray(
      1, this->_internal_null_value(), target);
  }

  // double number_value = 2;
  if (_internal_has_number_value()) {
    target = stream->EnsureSpace(target);
    target = ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::WriteDoubleToArray(2, this->_internal_number_value(), target);
  }

  // string string_value = 3;
  if (_internal_has_string_value()) {
    ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::VerifyUtf8String(
      this->_internal_string_value().data(), static_cast<int>(this->_internal_string_value().length()),
      ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::SERIALIZE,
      "google.protobuf.Value.string_value");
    target = stream->WriteStringMaybeAliased(
        3, this->_internal_string_value(), target);
  }

  // bool bool_value = 4;
  if (_internal_has_bool_value()) {
    target = stream->EnsureSpace(target);
    target = ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::WriteBoolToArray(4, this->_internal_bool_value(), target);
  }

  // .google.protobuf.Struct struct_value = 5;
  if (_internal_has_struct_value()) {
    target = stream->EnsureSpace(target);
    target = ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::
      InternalWriteMessage(
        5, _Internal::struct_value(this), target, stream);
  }

  // .google.protobuf.ListValue list_value = 6;
  if (_internal_has_list_value()) {
    target = stream->EnsureSpace(target);
    target = ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::
      InternalWriteMessage(
        6, _Internal::list_value(this), target, stream);
  }

  if (PROTOBUF_PREDICT_FALSE(_internal_metadata_.have_unknown_fields())) {
    target = ::PROTOBUF_NAMESPACE_ID::internal::WireFormat::InternalSerializeUnknownFieldsToArray(
        _internal_metadata_.unknown_fields<::PROTOBUF_NAMESPACE_ID::UnknownFieldSet>(::PROTOBUF_NAMESPACE_ID::UnknownFieldSet::default_instance), target, stream);
  }
  // @@protoc_insertion_point(serialize_to_array_end:google.protobuf.Value)
  return target;
}

size_t Value::ByteSizeLong() const {
// @@protoc_insertion_point(message_byte_size_start:google.protobuf.Value)
  size_t total_size = 0;

  arc_ui32 cached_has_bits = 0;
  // Prevent compiler warnings about cached_has_bits being unused
  (void) cached_has_bits;

  switch (kind_case()) {
    // .google.protobuf.NullValue null_value = 1;
    case kNullValue: {
      total_size += 1 +
        ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::EnumSize(this->_internal_null_value());
      break;
    }
    // double number_value = 2;
    case kNumberValue: {
      total_size += 1 + 8;
      break;
    }
    // string string_value = 3;
    case kStringValue: {
      total_size += 1 +
        ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::StringSize(
          this->_internal_string_value());
      break;
    }
    // bool bool_value = 4;
    case kBoolValue: {
      total_size += 1 + 1;
      break;
    }
    // .google.protobuf.Struct struct_value = 5;
    case kStructValue: {
      total_size += 1 +
        ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::MessageSize(
          *kind_.struct_value_);
      break;
    }
    // .google.protobuf.ListValue list_value = 6;
    case kListValue: {
      total_size += 1 +
        ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::MessageSize(
          *kind_.list_value_);
      break;
    }
    case KIND_NOT_SET: {
      break;
    }
  }
  return MaybeComputeUnknownFieldsSize(total_size, &_cached_size_);
}

const ::PROTOBUF_NAMESPACE_ID::Message::ClassData Value::_class_data_ = {
    ::PROTOBUF_NAMESPACE_ID::Message::CopyWithSizeCheck,
    Value::MergeImpl
};
const ::PROTOBUF_NAMESPACE_ID::Message::ClassData*Value::GetClassData() const { return &_class_data_; }

void Value::MergeImpl(::PROTOBUF_NAMESPACE_ID::Message* to,
                      const ::PROTOBUF_NAMESPACE_ID::Message& from) {
  static_cast<Value *>(to)->MergeFrom(
      static_cast<const Value &>(from));
}


void Value::MergeFrom(const Value& from) {
// @@protoc_insertion_point(class_specific_merge_from_start:google.protobuf.Value)
  GOOGLE_DCHECK_NE(&from, this);
  arc_ui32 cached_has_bits = 0;
  (void) cached_has_bits;

  switch (from.kind_case()) {
    case kNullValue: {
      _internal_set_null_value(from._internal_null_value());
      break;
    }
    case kNumberValue: {
      _internal_set_number_value(from._internal_number_value());
      break;
    }
    case kStringValue: {
      _internal_set_string_value(from._internal_string_value());
      break;
    }
    case kBoolValue: {
      _internal_set_bool_value(from._internal_bool_value());
      break;
    }
    case kStructValue: {
      _internal_mutable_struct_value()->::PROTOBUF_NAMESPACE_ID::Struct::MergeFrom(from._internal_struct_value());
      break;
    }
    case kListValue: {
      _internal_mutable_list_value()->::PROTOBUF_NAMESPACE_ID::ListValue::MergeFrom(from._internal_list_value());
      break;
    }
    case KIND_NOT_SET: {
      break;
    }
  }
  _internal_metadata_.MergeFrom<::PROTOBUF_NAMESPACE_ID::UnknownFieldSet>(from._internal_metadata_);
}

void Value::CopyFrom(const Value& from) {
// @@protoc_insertion_point(class_specific_copy_from_start:google.protobuf.Value)
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

bool Value::IsInitialized() const {
  return true;
}

void Value::InternalSwap(Value* other) {
  using std::swap;
  _internal_metadata_.InternalSwap(&other->_internal_metadata_);
  swap(kind_, other->kind_);
  swap(_oneof_case_[0], other->_oneof_case_[0]);
}

::PROTOBUF_NAMESPACE_ID::Metadata Value::GetMetadata() const {
  return ::PROTOBUF_NAMESPACE_ID::internal::AssignDescriptors(
      &descriptor_table_google_2fprotobuf_2fstruct_2eproto_getter, &descriptor_table_google_2fprotobuf_2fstruct_2eproto_once,
      file_level_metadata_google_2fprotobuf_2fstruct_2eproto[2]);
}

// ===================================================================

class ListValue::_Internal {
 public:
};

ListValue::ListValue(::PROTOBUF_NAMESPACE_ID::Arena* arena,
                         bool is_message_owned)
  : ::PROTOBUF_NAMESPACE_ID::Message(arena, is_message_owned),
  values_(arena) {
  SharedCtor();
  if (!is_message_owned) {
    RegisterArenaDtor(arena);
  }
  // @@protoc_insertion_point(arena_constructor:google.protobuf.ListValue)
}
ListValue::ListValue(const ListValue& from)
  : ::PROTOBUF_NAMESPACE_ID::Message(),
      values_(from.values_) {
  _internal_metadata_.MergeFrom<::PROTOBUF_NAMESPACE_ID::UnknownFieldSet>(from._internal_metadata_);
  // @@protoc_insertion_point(copy_constructor:google.protobuf.ListValue)
}

inline void ListValue::SharedCtor() {
}

ListValue::~ListValue() {
  // @@protoc_insertion_point(destructor:google.protobuf.ListValue)
  if (GetArenaForAllocation() != nullptr) return;
  SharedDtor();
  _internal_metadata_.Delete<::PROTOBUF_NAMESPACE_ID::UnknownFieldSet>();
}

inline void ListValue::SharedDtor() {
  GOOGLE_DCHECK(GetArenaForAllocation() == nullptr);
}

void ListValue::ArenaDtor(void* object) {
  ListValue* _this = reinterpret_cast< ListValue* >(object);
  (void)_this;
}
void ListValue::RegisterArenaDtor(::PROTOBUF_NAMESPACE_ID::Arena*) {
}
void ListValue::SetCachedSize(int size) const {
  _cached_size_.Set(size);
}

void ListValue::Clear() {
// @@protoc_insertion_point(message_clear_start:google.protobuf.ListValue)
  arc_ui32 cached_has_bits = 0;
  // Prevent compiler warnings about cached_has_bits being unused
  (void) cached_has_bits;

  values_.Clear();
  _internal_metadata_.Clear<::PROTOBUF_NAMESPACE_ID::UnknownFieldSet>();
}

const char* ListValue::_InternalParse(const char* ptr, ::PROTOBUF_NAMESPACE_ID::internal::ParseContext* ctx) {
#define CHK_(x) if (PROTOBUF_PREDICT_FALSE(!(x))) goto failure
  while (!ctx->Done(&ptr)) {
    arc_ui32 tag;
    ptr = ::PROTOBUF_NAMESPACE_ID::internal::ReadTag(ptr, &tag);
    switch (tag >> 3) {
      // repeated .google.protobuf.Value values = 1;
      case 1:
        if (PROTOBUF_PREDICT_TRUE(static_cast<uint8_t>(tag) == 10)) {
          ptr -= 1;
          do {
            ptr += 1;
            ptr = ctx->ParseMessage(_internal_add_values(), ptr);
            CHK_(ptr);
            if (!ctx->DataAvailable(ptr)) break;
          } while (::PROTOBUF_NAMESPACE_ID::internal::ExpectTag<10>(ptr));
        } else
          goto handle_unusual;
        continue;
      default:
        goto handle_unusual;
    }  // switch
  handle_unusual:
    if ((tag == 0) || ((tag & 7) == 4)) {
      CHK_(ptr);
      ctx->SetLastTag(tag);
      goto message_done;
    }
    ptr = UnknownFieldParse(
        tag,
        _internal_metadata_.mutable_unknown_fields<::PROTOBUF_NAMESPACE_ID::UnknownFieldSet>(),
        ptr, ctx);
    CHK_(ptr != nullptr);
  }  // while
message_done:
  return ptr;
failure:
  ptr = nullptr;
  goto message_done;
#undef CHK_
}

uint8_t* ListValue::_InternalSerialize(
    uint8_t* target, ::PROTOBUF_NAMESPACE_ID::io::EpsCopyOutputStream* stream) const {
  // @@protoc_insertion_point(serialize_to_array_start:google.protobuf.ListValue)
  arc_ui32 cached_has_bits = 0;
  (void) cached_has_bits;

  // repeated .google.protobuf.Value values = 1;
  for (unsigned int i = 0,
      n = static_cast<unsigned int>(this->_internal_values_size()); i < n; i++) {
    target = stream->EnsureSpace(target);
    target = ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::
      InternalWriteMessage(1, this->_internal_values(i), target, stream);
  }

  if (PROTOBUF_PREDICT_FALSE(_internal_metadata_.have_unknown_fields())) {
    target = ::PROTOBUF_NAMESPACE_ID::internal::WireFormat::InternalSerializeUnknownFieldsToArray(
        _internal_metadata_.unknown_fields<::PROTOBUF_NAMESPACE_ID::UnknownFieldSet>(::PROTOBUF_NAMESPACE_ID::UnknownFieldSet::default_instance), target, stream);
  }
  // @@protoc_insertion_point(serialize_to_array_end:google.protobuf.ListValue)
  return target;
}

size_t ListValue::ByteSizeLong() const {
// @@protoc_insertion_point(message_byte_size_start:google.protobuf.ListValue)
  size_t total_size = 0;

  arc_ui32 cached_has_bits = 0;
  // Prevent compiler warnings about cached_has_bits being unused
  (void) cached_has_bits;

  // repeated .google.protobuf.Value values = 1;
  total_size += 1UL * this->_internal_values_size();
  for (const auto& msg : this->values_) {
    total_size +=
      ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::MessageSize(msg);
  }

  return MaybeComputeUnknownFieldsSize(total_size, &_cached_size_);
}

const ::PROTOBUF_NAMESPACE_ID::Message::ClassData ListValue::_class_data_ = {
    ::PROTOBUF_NAMESPACE_ID::Message::CopyWithSizeCheck,
    ListValue::MergeImpl
};
const ::PROTOBUF_NAMESPACE_ID::Message::ClassData*ListValue::GetClassData() const { return &_class_data_; }

void ListValue::MergeImpl(::PROTOBUF_NAMESPACE_ID::Message* to,
                      const ::PROTOBUF_NAMESPACE_ID::Message& from) {
  static_cast<ListValue *>(to)->MergeFrom(
      static_cast<const ListValue &>(from));
}


void ListValue::MergeFrom(const ListValue& from) {
// @@protoc_insertion_point(class_specific_merge_from_start:google.protobuf.ListValue)
  GOOGLE_DCHECK_NE(&from, this);
  arc_ui32 cached_has_bits = 0;
  (void) cached_has_bits;

  values_.MergeFrom(from.values_);
  _internal_metadata_.MergeFrom<::PROTOBUF_NAMESPACE_ID::UnknownFieldSet>(from._internal_metadata_);
}

void ListValue::CopyFrom(const ListValue& from) {
// @@protoc_insertion_point(class_specific_copy_from_start:google.protobuf.ListValue)
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

bool ListValue::IsInitialized() const {
  return true;
}

void ListValue::InternalSwap(ListValue* other) {
  using std::swap;
  _internal_metadata_.InternalSwap(&other->_internal_metadata_);
  values_.InternalSwap(&other->values_);
}

::PROTOBUF_NAMESPACE_ID::Metadata ListValue::GetMetadata() const {
  return ::PROTOBUF_NAMESPACE_ID::internal::AssignDescriptors(
      &descriptor_table_google_2fprotobuf_2fstruct_2eproto_getter, &descriptor_table_google_2fprotobuf_2fstruct_2eproto_once,
      file_level_metadata_google_2fprotobuf_2fstruct_2eproto[3]);
}

// @@protoc_insertion_point(namespace_scope)
PROTOBUF_NAMESPACE_CLOSE
PROTOBUF_NAMESPACE_OPEN
template<> PROTOBUF_NOINLINE ::PROTOBUF_NAMESPACE_ID::Struct_FieldsEntry_DoNotUse* Arena::CreateMaybeMessage< ::PROTOBUF_NAMESPACE_ID::Struct_FieldsEntry_DoNotUse >(Arena* arena) {
  return Arena::CreateMessageInternal< ::PROTOBUF_NAMESPACE_ID::Struct_FieldsEntry_DoNotUse >(arena);
}
template<> PROTOBUF_NOINLINE ::PROTOBUF_NAMESPACE_ID::Struct* Arena::CreateMaybeMessage< ::PROTOBUF_NAMESPACE_ID::Struct >(Arena* arena) {
  return Arena::CreateMessageInternal< ::PROTOBUF_NAMESPACE_ID::Struct >(arena);
}
template<> PROTOBUF_NOINLINE ::PROTOBUF_NAMESPACE_ID::Value* Arena::CreateMaybeMessage< ::PROTOBUF_NAMESPACE_ID::Value >(Arena* arena) {
  return Arena::CreateMessageInternal< ::PROTOBUF_NAMESPACE_ID::Value >(arena);
}
template<> PROTOBUF_NOINLINE ::PROTOBUF_NAMESPACE_ID::ListValue* Arena::CreateMaybeMessage< ::PROTOBUF_NAMESPACE_ID::ListValue >(Arena* arena) {
  return Arena::CreateMessageInternal< ::PROTOBUF_NAMESPACE_ID::ListValue >(arena);
}
PROTOBUF_NAMESPACE_CLOSE

// @@protoc_insertion_point(global_scope)
#include <google/protobuf/port_undef.inc>
