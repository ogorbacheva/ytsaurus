// Code generated by protoc-gen-go. DO NOT EDIT.
// versions:
// 	protoc-gen-go v1.26.0
// 	protoc        v3.19.0
// source: yt/yt_proto/yt/core/ytree/proto/attributes.proto

package ytree

import (
	_ "github.com/doublecloud/transfer/yt/go/proto/core/yson"
	protoreflect "google.golang.org/protobuf/reflect/protoreflect"
	protoimpl "google.golang.org/protobuf/runtime/protoimpl"
	reflect "reflect"
	sync "sync"
)

const (
	// Verify that this generated code is sufficiently up-to-date.
	_ = protoimpl.EnforceVersion(20 - protoimpl.MinVersion)
	// Verify that runtime/protoimpl is sufficiently up-to-date.
	_ = protoimpl.EnforceVersion(protoimpl.MaxVersion - 20)
)

type TAttribute struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Key   *string `protobuf:"bytes,1,req,name=key" json:"key,omitempty"`
	Value []byte  `protobuf:"bytes,2,req,name=value" json:"value,omitempty"`
}

func (x *TAttribute) Reset() {
	*x = TAttribute{}
	if protoimpl.UnsafeEnabled {
		mi := &file_yt_yt_proto_yt_core_ytree_proto_attributes_proto_msgTypes[0]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TAttribute) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TAttribute) ProtoMessage() {}

func (x *TAttribute) ProtoReflect() protoreflect.Message {
	mi := &file_yt_yt_proto_yt_core_ytree_proto_attributes_proto_msgTypes[0]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TAttribute.ProtoReflect.Descriptor instead.
func (*TAttribute) Descriptor() ([]byte, []int) {
	return file_yt_yt_proto_yt_core_ytree_proto_attributes_proto_rawDescGZIP(), []int{0}
}

func (x *TAttribute) GetKey() string {
	if x != nil && x.Key != nil {
		return *x.Key
	}
	return ""
}

func (x *TAttribute) GetValue() []byte {
	if x != nil {
		return x.Value
	}
	return nil
}

type TAttributeDictionary struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Attributes []*TAttribute `protobuf:"bytes,1,rep,name=attributes" json:"attributes,omitempty"`
}

func (x *TAttributeDictionary) Reset() {
	*x = TAttributeDictionary{}
	if protoimpl.UnsafeEnabled {
		mi := &file_yt_yt_proto_yt_core_ytree_proto_attributes_proto_msgTypes[1]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TAttributeDictionary) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TAttributeDictionary) ProtoMessage() {}

func (x *TAttributeDictionary) ProtoReflect() protoreflect.Message {
	mi := &file_yt_yt_proto_yt_core_ytree_proto_attributes_proto_msgTypes[1]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TAttributeDictionary.ProtoReflect.Descriptor instead.
func (*TAttributeDictionary) Descriptor() ([]byte, []int) {
	return file_yt_yt_proto_yt_core_ytree_proto_attributes_proto_rawDescGZIP(), []int{1}
}

func (x *TAttributeDictionary) GetAttributes() []*TAttribute {
	if x != nil {
		return x.Attributes
	}
	return nil
}

type TAttributeFilter struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Keys  []string `protobuf:"bytes,1,rep,name=keys" json:"keys,omitempty"`
	Paths []string `protobuf:"bytes,2,rep,name=paths" json:"paths,omitempty"`
}

func (x *TAttributeFilter) Reset() {
	*x = TAttributeFilter{}
	if protoimpl.UnsafeEnabled {
		mi := &file_yt_yt_proto_yt_core_ytree_proto_attributes_proto_msgTypes[2]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TAttributeFilter) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TAttributeFilter) ProtoMessage() {}

func (x *TAttributeFilter) ProtoReflect() protoreflect.Message {
	mi := &file_yt_yt_proto_yt_core_ytree_proto_attributes_proto_msgTypes[2]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TAttributeFilter.ProtoReflect.Descriptor instead.
func (*TAttributeFilter) Descriptor() ([]byte, []int) {
	return file_yt_yt_proto_yt_core_ytree_proto_attributes_proto_rawDescGZIP(), []int{2}
}

func (x *TAttributeFilter) GetKeys() []string {
	if x != nil {
		return x.Keys
	}
	return nil
}

func (x *TAttributeFilter) GetPaths() []string {
	if x != nil {
		return x.Paths
	}
	return nil
}

var File_yt_yt_proto_yt_core_ytree_proto_attributes_proto protoreflect.FileDescriptor

var file_yt_yt_proto_yt_core_ytree_proto_attributes_proto_rawDesc = []byte{
	0x0a, 0x30, 0x79, 0x74, 0x2f, 0x79, 0x74, 0x5f, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2f, 0x79, 0x74,
	0x2f, 0x63, 0x6f, 0x72, 0x65, 0x2f, 0x79, 0x74, 0x72, 0x65, 0x65, 0x2f, 0x70, 0x72, 0x6f, 0x74,
	0x6f, 0x2f, 0x61, 0x74, 0x74, 0x72, 0x69, 0x62, 0x75, 0x74, 0x65, 0x73, 0x2e, 0x70, 0x72, 0x6f,
	0x74, 0x6f, 0x12, 0x11, 0x4e, 0x59, 0x54, 0x2e, 0x4e, 0x59, 0x54, 0x72, 0x65, 0x65, 0x2e, 0x4e,
	0x50, 0x72, 0x6f, 0x74, 0x6f, 0x1a, 0x32, 0x79, 0x74, 0x5f, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2f,
	0x79, 0x74, 0x2f, 0x63, 0x6f, 0x72, 0x65, 0x2f, 0x79, 0x73, 0x6f, 0x6e, 0x2f, 0x70, 0x72, 0x6f,
	0x74, 0x6f, 0x2f, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x62, 0x75, 0x66, 0x5f, 0x69, 0x6e, 0x74, 0x65,
	0x72, 0x6f, 0x70, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x22, 0x34, 0x0a, 0x0a, 0x54, 0x41, 0x74,
	0x74, 0x72, 0x69, 0x62, 0x75, 0x74, 0x65, 0x12, 0x10, 0x0a, 0x03, 0x6b, 0x65, 0x79, 0x18, 0x01,
	0x20, 0x02, 0x28, 0x09, 0x52, 0x03, 0x6b, 0x65, 0x79, 0x12, 0x14, 0x0a, 0x05, 0x76, 0x61, 0x6c,
	0x75, 0x65, 0x18, 0x02, 0x20, 0x02, 0x28, 0x0c, 0x52, 0x05, 0x76, 0x61, 0x6c, 0x75, 0x65, 0x22,
	0x5b, 0x0a, 0x14, 0x54, 0x41, 0x74, 0x74, 0x72, 0x69, 0x62, 0x75, 0x74, 0x65, 0x44, 0x69, 0x63,
	0x74, 0x69, 0x6f, 0x6e, 0x61, 0x72, 0x79, 0x12, 0x3d, 0x0a, 0x0a, 0x61, 0x74, 0x74, 0x72, 0x69,
	0x62, 0x75, 0x74, 0x65, 0x73, 0x18, 0x01, 0x20, 0x03, 0x28, 0x0b, 0x32, 0x1d, 0x2e, 0x4e, 0x59,
	0x54, 0x2e, 0x4e, 0x59, 0x54, 0x72, 0x65, 0x65, 0x2e, 0x4e, 0x50, 0x72, 0x6f, 0x74, 0x6f, 0x2e,
	0x54, 0x41, 0x74, 0x74, 0x72, 0x69, 0x62, 0x75, 0x74, 0x65, 0x52, 0x0a, 0x61, 0x74, 0x74, 0x72,
	0x69, 0x62, 0x75, 0x74, 0x65, 0x73, 0x3a, 0x04, 0xc0, 0xbb, 0x01, 0x01, 0x22, 0x3c, 0x0a, 0x10,
	0x54, 0x41, 0x74, 0x74, 0x72, 0x69, 0x62, 0x75, 0x74, 0x65, 0x46, 0x69, 0x6c, 0x74, 0x65, 0x72,
	0x12, 0x12, 0x0a, 0x04, 0x6b, 0x65, 0x79, 0x73, 0x18, 0x01, 0x20, 0x03, 0x28, 0x09, 0x52, 0x04,
	0x6b, 0x65, 0x79, 0x73, 0x12, 0x14, 0x0a, 0x05, 0x70, 0x61, 0x74, 0x68, 0x73, 0x18, 0x02, 0x20,
	0x03, 0x28, 0x09, 0x52, 0x05, 0x70, 0x61, 0x74, 0x68, 0x73, 0x42, 0x40, 0x0a, 0x13, 0x74, 0x65,
	0x63, 0x68, 0x2e, 0x79, 0x74, 0x73, 0x61, 0x75, 0x72, 0x75, 0x73, 0x2e, 0x79, 0x74, 0x72, 0x65,
	0x65, 0x50, 0x01, 0x5a, 0x27, 0x61, 0x2e, 0x79, 0x61, 0x6e, 0x64, 0x65, 0x78, 0x2d, 0x74, 0x65,
	0x61, 0x6d, 0x2e, 0x72, 0x75, 0x2f, 0x79, 0x74, 0x2f, 0x67, 0x6f, 0x2f, 0x70, 0x72, 0x6f, 0x74,
	0x6f, 0x2f, 0x63, 0x6f, 0x72, 0x65, 0x2f, 0x79, 0x74, 0x72, 0x65, 0x65,
}

var (
	file_yt_yt_proto_yt_core_ytree_proto_attributes_proto_rawDescOnce sync.Once
	file_yt_yt_proto_yt_core_ytree_proto_attributes_proto_rawDescData = file_yt_yt_proto_yt_core_ytree_proto_attributes_proto_rawDesc
)

func file_yt_yt_proto_yt_core_ytree_proto_attributes_proto_rawDescGZIP() []byte {
	file_yt_yt_proto_yt_core_ytree_proto_attributes_proto_rawDescOnce.Do(func() {
		file_yt_yt_proto_yt_core_ytree_proto_attributes_proto_rawDescData = protoimpl.X.CompressGZIP(file_yt_yt_proto_yt_core_ytree_proto_attributes_proto_rawDescData)
	})
	return file_yt_yt_proto_yt_core_ytree_proto_attributes_proto_rawDescData
}

var file_yt_yt_proto_yt_core_ytree_proto_attributes_proto_msgTypes = make([]protoimpl.MessageInfo, 3)
var file_yt_yt_proto_yt_core_ytree_proto_attributes_proto_goTypes = []interface{}{
	(*TAttribute)(nil),           // 0: NYT.NYTree.NProto.TAttribute
	(*TAttributeDictionary)(nil), // 1: NYT.NYTree.NProto.TAttributeDictionary
	(*TAttributeFilter)(nil),     // 2: NYT.NYTree.NProto.TAttributeFilter
}
var file_yt_yt_proto_yt_core_ytree_proto_attributes_proto_depIdxs = []int32{
	0, // 0: NYT.NYTree.NProto.TAttributeDictionary.attributes:type_name -> NYT.NYTree.NProto.TAttribute
	1, // [1:1] is the sub-list for method output_type
	1, // [1:1] is the sub-list for method input_type
	1, // [1:1] is the sub-list for extension type_name
	1, // [1:1] is the sub-list for extension extendee
	0, // [0:1] is the sub-list for field type_name
}

func init() { file_yt_yt_proto_yt_core_ytree_proto_attributes_proto_init() }
func file_yt_yt_proto_yt_core_ytree_proto_attributes_proto_init() {
	if File_yt_yt_proto_yt_core_ytree_proto_attributes_proto != nil {
		return
	}
	if !protoimpl.UnsafeEnabled {
		file_yt_yt_proto_yt_core_ytree_proto_attributes_proto_msgTypes[0].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TAttribute); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
		file_yt_yt_proto_yt_core_ytree_proto_attributes_proto_msgTypes[1].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TAttributeDictionary); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
		file_yt_yt_proto_yt_core_ytree_proto_attributes_proto_msgTypes[2].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TAttributeFilter); i {
			case 0:
				return &v.state
			case 1:
				return &v.sizeCache
			case 2:
				return &v.unknownFields
			default:
				return nil
			}
		}
	}
	type x struct{}
	out := protoimpl.TypeBuilder{
		File: protoimpl.DescBuilder{
			GoPackagePath: reflect.TypeOf(x{}).PkgPath(),
			RawDescriptor: file_yt_yt_proto_yt_core_ytree_proto_attributes_proto_rawDesc,
			NumEnums:      0,
			NumMessages:   3,
			NumExtensions: 0,
			NumServices:   0,
		},
		GoTypes:           file_yt_yt_proto_yt_core_ytree_proto_attributes_proto_goTypes,
		DependencyIndexes: file_yt_yt_proto_yt_core_ytree_proto_attributes_proto_depIdxs,
		MessageInfos:      file_yt_yt_proto_yt_core_ytree_proto_attributes_proto_msgTypes,
	}.Build()
	File_yt_yt_proto_yt_core_ytree_proto_attributes_proto = out.File
	file_yt_yt_proto_yt_core_ytree_proto_attributes_proto_rawDesc = nil
	file_yt_yt_proto_yt_core_ytree_proto_attributes_proto_goTypes = nil
	file_yt_yt_proto_yt_core_ytree_proto_attributes_proto_depIdxs = nil
}
