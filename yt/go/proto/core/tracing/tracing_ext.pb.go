// Code generated by protoc-gen-go. DO NOT EDIT.
// versions:
// 	protoc-gen-go v1.28.1
// 	protoc        v3.15.8
// source: yt_proto/yt/core/tracing/proto/tracing_ext.proto

package tracing

import (
	protoreflect "google.golang.org/protobuf/reflect/protoreflect"
	protoimpl "google.golang.org/protobuf/runtime/protoimpl"
	reflect "reflect"
	sync "sync"
	misc "go.ytsaurus.tech/yt/go/proto/core/misc"
)

const (
	// Verify that this generated code is sufficiently up-to-date.
	_ = protoimpl.EnforceVersion(20 - protoimpl.MinVersion)
	// Verify that runtime/protoimpl is sufficiently up-to-date.
	_ = protoimpl.EnforceVersion(protoimpl.MaxVersion - 20)
)

type TTracingExt struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	TraceId        *misc.TGuid `protobuf:"bytes,4,opt,name=trace_id,json=traceId" json:"trace_id,omitempty"`
	SpanId         *uint64     `protobuf:"fixed64,5,opt,name=span_id,json=spanId" json:"span_id,omitempty"`
	Sampled        *bool       `protobuf:"varint,6,opt,name=sampled" json:"sampled,omitempty"`
	Debug          *bool       `protobuf:"varint,7,opt,name=debug" json:"debug,omitempty"`
	TargetEndpoint *string     `protobuf:"bytes,9,opt,name=target_endpoint,json=targetEndpoint" json:"target_endpoint,omitempty"`
	Baggage        []byte      `protobuf:"bytes,8,opt,name=baggage" json:"baggage,omitempty"`
}

func (x *TTracingExt) Reset() {
	*x = TTracingExt{}
	if protoimpl.UnsafeEnabled {
		mi := &file_yt_proto_yt_core_tracing_proto_tracing_ext_proto_msgTypes[0]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TTracingExt) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TTracingExt) ProtoMessage() {}

func (x *TTracingExt) ProtoReflect() protoreflect.Message {
	mi := &file_yt_proto_yt_core_tracing_proto_tracing_ext_proto_msgTypes[0]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TTracingExt.ProtoReflect.Descriptor instead.
func (*TTracingExt) Descriptor() ([]byte, []int) {
	return file_yt_proto_yt_core_tracing_proto_tracing_ext_proto_rawDescGZIP(), []int{0}
}

func (x *TTracingExt) GetTraceId() *misc.TGuid {
	if x != nil {
		return x.TraceId
	}
	return nil
}

func (x *TTracingExt) GetSpanId() uint64 {
	if x != nil && x.SpanId != nil {
		return *x.SpanId
	}
	return 0
}

func (x *TTracingExt) GetSampled() bool {
	if x != nil && x.Sampled != nil {
		return *x.Sampled
	}
	return false
}

func (x *TTracingExt) GetDebug() bool {
	if x != nil && x.Debug != nil {
		return *x.Debug
	}
	return false
}

func (x *TTracingExt) GetTargetEndpoint() string {
	if x != nil && x.TargetEndpoint != nil {
		return *x.TargetEndpoint
	}
	return ""
}

func (x *TTracingExt) GetBaggage() []byte {
	if x != nil {
		return x.Baggage
	}
	return nil
}

var File_yt_proto_yt_core_tracing_proto_tracing_ext_proto protoreflect.FileDescriptor

var file_yt_proto_yt_core_tracing_proto_tracing_ext_proto_rawDesc = []byte{
	0x0a, 0x30, 0x79, 0x74, 0x5f, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2f, 0x79, 0x74, 0x2f, 0x63, 0x6f,
	0x72, 0x65, 0x2f, 0x74, 0x72, 0x61, 0x63, 0x69, 0x6e, 0x67, 0x2f, 0x70, 0x72, 0x6f, 0x74, 0x6f,
	0x2f, 0x74, 0x72, 0x61, 0x63, 0x69, 0x6e, 0x67, 0x5f, 0x65, 0x78, 0x74, 0x2e, 0x70, 0x72, 0x6f,
	0x74, 0x6f, 0x12, 0x13, 0x4e, 0x59, 0x54, 0x2e, 0x4e, 0x54, 0x72, 0x61, 0x63, 0x69, 0x6e, 0x67,
	0x2e, 0x4e, 0x50, 0x72, 0x6f, 0x74, 0x6f, 0x1a, 0x26, 0x79, 0x74, 0x5f, 0x70, 0x72, 0x6f, 0x74,
	0x6f, 0x2f, 0x79, 0x74, 0x2f, 0x63, 0x6f, 0x72, 0x65, 0x2f, 0x6d, 0x69, 0x73, 0x63, 0x2f, 0x70,
	0x72, 0x6f, 0x74, 0x6f, 0x2f, 0x67, 0x75, 0x69, 0x64, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x22,
	0xd9, 0x01, 0x0a, 0x0b, 0x54, 0x54, 0x72, 0x61, 0x63, 0x69, 0x6e, 0x67, 0x45, 0x78, 0x74, 0x12,
	0x2c, 0x0a, 0x08, 0x74, 0x72, 0x61, 0x63, 0x65, 0x5f, 0x69, 0x64, 0x18, 0x04, 0x20, 0x01, 0x28,
	0x0b, 0x32, 0x11, 0x2e, 0x4e, 0x59, 0x54, 0x2e, 0x4e, 0x50, 0x72, 0x6f, 0x74, 0x6f, 0x2e, 0x54,
	0x47, 0x75, 0x69, 0x64, 0x52, 0x07, 0x74, 0x72, 0x61, 0x63, 0x65, 0x49, 0x64, 0x12, 0x17, 0x0a,
	0x07, 0x73, 0x70, 0x61, 0x6e, 0x5f, 0x69, 0x64, 0x18, 0x05, 0x20, 0x01, 0x28, 0x06, 0x52, 0x06,
	0x73, 0x70, 0x61, 0x6e, 0x49, 0x64, 0x12, 0x18, 0x0a, 0x07, 0x73, 0x61, 0x6d, 0x70, 0x6c, 0x65,
	0x64, 0x18, 0x06, 0x20, 0x01, 0x28, 0x08, 0x52, 0x07, 0x73, 0x61, 0x6d, 0x70, 0x6c, 0x65, 0x64,
	0x12, 0x14, 0x0a, 0x05, 0x64, 0x65, 0x62, 0x75, 0x67, 0x18, 0x07, 0x20, 0x01, 0x28, 0x08, 0x52,
	0x05, 0x64, 0x65, 0x62, 0x75, 0x67, 0x12, 0x27, 0x0a, 0x0f, 0x74, 0x61, 0x72, 0x67, 0x65, 0x74,
	0x5f, 0x65, 0x6e, 0x64, 0x70, 0x6f, 0x69, 0x6e, 0x74, 0x18, 0x09, 0x20, 0x01, 0x28, 0x09, 0x52,
	0x0e, 0x74, 0x61, 0x72, 0x67, 0x65, 0x74, 0x45, 0x6e, 0x64, 0x70, 0x6f, 0x69, 0x6e, 0x74, 0x12,
	0x18, 0x0a, 0x07, 0x62, 0x61, 0x67, 0x67, 0x61, 0x67, 0x65, 0x18, 0x08, 0x20, 0x01, 0x28, 0x0c,
	0x52, 0x07, 0x62, 0x61, 0x67, 0x67, 0x61, 0x67, 0x65, 0x4a, 0x04, 0x08, 0x01, 0x10, 0x02, 0x4a,
	0x04, 0x08, 0x02, 0x10, 0x03, 0x4a, 0x04, 0x08, 0x03, 0x10, 0x04, 0x42, 0x41, 0x0a, 0x15, 0x74,
	0x65, 0x63, 0x68, 0x2e, 0x79, 0x74, 0x73, 0x61, 0x75, 0x72, 0x75, 0x73, 0x2e, 0x74, 0x72, 0x61,
	0x63, 0x69, 0x6e, 0x67, 0x50, 0x01, 0x5a, 0x26, 0x79, 0x74, 0x73, 0x61, 0x75, 0x72, 0x75, 0x73,
	0x2e, 0x74, 0x65, 0x63, 0x68, 0x2f, 0x79, 0x74, 0x2f, 0x67, 0x6f, 0x2f, 0x70, 0x72, 0x6f, 0x74,
	0x6f, 0x2f, 0x63, 0x6f, 0x72, 0x65, 0x2f, 0x74, 0x72, 0x61, 0x63, 0x69, 0x6e, 0x67,
}

var (
	file_yt_proto_yt_core_tracing_proto_tracing_ext_proto_rawDescOnce sync.Once
	file_yt_proto_yt_core_tracing_proto_tracing_ext_proto_rawDescData = file_yt_proto_yt_core_tracing_proto_tracing_ext_proto_rawDesc
)

func file_yt_proto_yt_core_tracing_proto_tracing_ext_proto_rawDescGZIP() []byte {
	file_yt_proto_yt_core_tracing_proto_tracing_ext_proto_rawDescOnce.Do(func() {
		file_yt_proto_yt_core_tracing_proto_tracing_ext_proto_rawDescData = protoimpl.X.CompressGZIP(file_yt_proto_yt_core_tracing_proto_tracing_ext_proto_rawDescData)
	})
	return file_yt_proto_yt_core_tracing_proto_tracing_ext_proto_rawDescData
}

var file_yt_proto_yt_core_tracing_proto_tracing_ext_proto_msgTypes = make([]protoimpl.MessageInfo, 1)
var file_yt_proto_yt_core_tracing_proto_tracing_ext_proto_goTypes = []interface{}{
	(*TTracingExt)(nil), // 0: NYT.NTracing.NProto.TTracingExt
	(*misc.TGuid)(nil),  // 1: NYT.NProto.TGuid
}
var file_yt_proto_yt_core_tracing_proto_tracing_ext_proto_depIdxs = []int32{
	1, // 0: NYT.NTracing.NProto.TTracingExt.trace_id:type_name -> NYT.NProto.TGuid
	1, // [1:1] is the sub-list for method output_type
	1, // [1:1] is the sub-list for method input_type
	1, // [1:1] is the sub-list for extension type_name
	1, // [1:1] is the sub-list for extension extendee
	0, // [0:1] is the sub-list for field type_name
}

func init() { file_yt_proto_yt_core_tracing_proto_tracing_ext_proto_init() }
func file_yt_proto_yt_core_tracing_proto_tracing_ext_proto_init() {
	if File_yt_proto_yt_core_tracing_proto_tracing_ext_proto != nil {
		return
	}
	if !protoimpl.UnsafeEnabled {
		file_yt_proto_yt_core_tracing_proto_tracing_ext_proto_msgTypes[0].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TTracingExt); i {
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
			RawDescriptor: file_yt_proto_yt_core_tracing_proto_tracing_ext_proto_rawDesc,
			NumEnums:      0,
			NumMessages:   1,
			NumExtensions: 0,
			NumServices:   0,
		},
		GoTypes:           file_yt_proto_yt_core_tracing_proto_tracing_ext_proto_goTypes,
		DependencyIndexes: file_yt_proto_yt_core_tracing_proto_tracing_ext_proto_depIdxs,
		MessageInfos:      file_yt_proto_yt_core_tracing_proto_tracing_ext_proto_msgTypes,
	}.Build()
	File_yt_proto_yt_core_tracing_proto_tracing_ext_proto = out.File
	file_yt_proto_yt_core_tracing_proto_tracing_ext_proto_rawDesc = nil
	file_yt_proto_yt_core_tracing_proto_tracing_ext_proto_goTypes = nil
	file_yt_proto_yt_core_tracing_proto_tracing_ext_proto_depIdxs = nil
}
