// Code generated by protoc-gen-go. DO NOT EDIT.
// versions:
// 	protoc-gen-go v1.28.1
// 	protoc        v3.19.0
// source: yt/yt_proto/yt/core/tracing/proto/span.proto

package tracing

import (
	misc "go.ytsaurus.tech/yt/go/proto/core/misc"
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

type TTag struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Key   *string `protobuf:"bytes,1,req,name=key" json:"key,omitempty"`
	Value *string `protobuf:"bytes,2,req,name=value" json:"value,omitempty"`
}

func (x *TTag) Reset() {
	*x = TTag{}
	if protoimpl.UnsafeEnabled {
		mi := &file_yt_yt_proto_yt_core_tracing_proto_span_proto_msgTypes[0]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TTag) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TTag) ProtoMessage() {}

func (x *TTag) ProtoReflect() protoreflect.Message {
	mi := &file_yt_yt_proto_yt_core_tracing_proto_span_proto_msgTypes[0]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TTag.ProtoReflect.Descriptor instead.
func (*TTag) Descriptor() ([]byte, []int) {
	return file_yt_yt_proto_yt_core_tracing_proto_span_proto_rawDescGZIP(), []int{0}
}

func (x *TTag) GetKey() string {
	if x != nil && x.Key != nil {
		return *x.Key
	}
	return ""
}

func (x *TTag) GetValue() string {
	if x != nil && x.Value != nil {
		return *x.Value
	}
	return ""
}

type TSpan struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	TraceId           *misc.TGuid `protobuf:"bytes,1,req,name=trace_id,json=traceId" json:"trace_id,omitempty"`
	SpanId            *uint64     `protobuf:"fixed64,2,req,name=span_id,json=spanId" json:"span_id,omitempty"`
	ParentSpanId      *uint64     `protobuf:"fixed64,3,opt,name=parent_span_id,json=parentSpanId" json:"parent_span_id,omitempty"`
	FollowsFromSpanId *uint64     `protobuf:"fixed64,4,opt,name=follows_from_span_id,json=followsFromSpanId" json:"follows_from_span_id,omitempty"`
	Name              *string     `protobuf:"bytes,5,req,name=name" json:"name,omitempty"`
	StartTime         *uint64     `protobuf:"fixed64,6,req,name=start_time,json=startTime" json:"start_time,omitempty"`
	Duration          *uint64     `protobuf:"fixed64,7,req,name=duration" json:"duration,omitempty"`
	Tags              []*TTag     `protobuf:"bytes,8,rep,name=tags" json:"tags,omitempty"`
}

func (x *TSpan) Reset() {
	*x = TSpan{}
	if protoimpl.UnsafeEnabled {
		mi := &file_yt_yt_proto_yt_core_tracing_proto_span_proto_msgTypes[1]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TSpan) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TSpan) ProtoMessage() {}

func (x *TSpan) ProtoReflect() protoreflect.Message {
	mi := &file_yt_yt_proto_yt_core_tracing_proto_span_proto_msgTypes[1]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TSpan.ProtoReflect.Descriptor instead.
func (*TSpan) Descriptor() ([]byte, []int) {
	return file_yt_yt_proto_yt_core_tracing_proto_span_proto_rawDescGZIP(), []int{1}
}

func (x *TSpan) GetTraceId() *misc.TGuid {
	if x != nil {
		return x.TraceId
	}
	return nil
}

func (x *TSpan) GetSpanId() uint64 {
	if x != nil && x.SpanId != nil {
		return *x.SpanId
	}
	return 0
}

func (x *TSpan) GetParentSpanId() uint64 {
	if x != nil && x.ParentSpanId != nil {
		return *x.ParentSpanId
	}
	return 0
}

func (x *TSpan) GetFollowsFromSpanId() uint64 {
	if x != nil && x.FollowsFromSpanId != nil {
		return *x.FollowsFromSpanId
	}
	return 0
}

func (x *TSpan) GetName() string {
	if x != nil && x.Name != nil {
		return *x.Name
	}
	return ""
}

func (x *TSpan) GetStartTime() uint64 {
	if x != nil && x.StartTime != nil {
		return *x.StartTime
	}
	return 0
}

func (x *TSpan) GetDuration() uint64 {
	if x != nil && x.Duration != nil {
		return *x.Duration
	}
	return 0
}

func (x *TSpan) GetTags() []*TTag {
	if x != nil {
		return x.Tags
	}
	return nil
}

type TSpanBatch struct {
	state         protoimpl.MessageState
	sizeCache     protoimpl.SizeCache
	unknownFields protoimpl.UnknownFields

	Spans []*TSpan `protobuf:"bytes,1,rep,name=spans" json:"spans,omitempty"`
}

func (x *TSpanBatch) Reset() {
	*x = TSpanBatch{}
	if protoimpl.UnsafeEnabled {
		mi := &file_yt_yt_proto_yt_core_tracing_proto_span_proto_msgTypes[2]
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		ms.StoreMessageInfo(mi)
	}
}

func (x *TSpanBatch) String() string {
	return protoimpl.X.MessageStringOf(x)
}

func (*TSpanBatch) ProtoMessage() {}

func (x *TSpanBatch) ProtoReflect() protoreflect.Message {
	mi := &file_yt_yt_proto_yt_core_tracing_proto_span_proto_msgTypes[2]
	if protoimpl.UnsafeEnabled && x != nil {
		ms := protoimpl.X.MessageStateOf(protoimpl.Pointer(x))
		if ms.LoadMessageInfo() == nil {
			ms.StoreMessageInfo(mi)
		}
		return ms
	}
	return mi.MessageOf(x)
}

// Deprecated: Use TSpanBatch.ProtoReflect.Descriptor instead.
func (*TSpanBatch) Descriptor() ([]byte, []int) {
	return file_yt_yt_proto_yt_core_tracing_proto_span_proto_rawDescGZIP(), []int{2}
}

func (x *TSpanBatch) GetSpans() []*TSpan {
	if x != nil {
		return x.Spans
	}
	return nil
}

var File_yt_yt_proto_yt_core_tracing_proto_span_proto protoreflect.FileDescriptor

var file_yt_yt_proto_yt_core_tracing_proto_span_proto_rawDesc = []byte{
	0x0a, 0x2c, 0x79, 0x74, 0x2f, 0x79, 0x74, 0x5f, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2f, 0x79, 0x74,
	0x2f, 0x63, 0x6f, 0x72, 0x65, 0x2f, 0x74, 0x72, 0x61, 0x63, 0x69, 0x6e, 0x67, 0x2f, 0x70, 0x72,
	0x6f, 0x74, 0x6f, 0x2f, 0x73, 0x70, 0x61, 0x6e, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x12, 0x13,
	0x4e, 0x59, 0x54, 0x2e, 0x4e, 0x54, 0x72, 0x61, 0x63, 0x69, 0x6e, 0x67, 0x2e, 0x4e, 0x50, 0x72,
	0x6f, 0x74, 0x6f, 0x1a, 0x26, 0x79, 0x74, 0x5f, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2f, 0x79, 0x74,
	0x2f, 0x63, 0x6f, 0x72, 0x65, 0x2f, 0x6d, 0x69, 0x73, 0x63, 0x2f, 0x70, 0x72, 0x6f, 0x74, 0x6f,
	0x2f, 0x67, 0x75, 0x69, 0x64, 0x2e, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x22, 0x2e, 0x0a, 0x04, 0x54,
	0x54, 0x61, 0x67, 0x12, 0x10, 0x0a, 0x03, 0x6b, 0x65, 0x79, 0x18, 0x01, 0x20, 0x02, 0x28, 0x09,
	0x52, 0x03, 0x6b, 0x65, 0x79, 0x12, 0x14, 0x0a, 0x05, 0x76, 0x61, 0x6c, 0x75, 0x65, 0x18, 0x02,
	0x20, 0x02, 0x28, 0x09, 0x52, 0x05, 0x76, 0x61, 0x6c, 0x75, 0x65, 0x22, 0xa3, 0x02, 0x0a, 0x05,
	0x54, 0x53, 0x70, 0x61, 0x6e, 0x12, 0x2c, 0x0a, 0x08, 0x74, 0x72, 0x61, 0x63, 0x65, 0x5f, 0x69,
	0x64, 0x18, 0x01, 0x20, 0x02, 0x28, 0x0b, 0x32, 0x11, 0x2e, 0x4e, 0x59, 0x54, 0x2e, 0x4e, 0x50,
	0x72, 0x6f, 0x74, 0x6f, 0x2e, 0x54, 0x47, 0x75, 0x69, 0x64, 0x52, 0x07, 0x74, 0x72, 0x61, 0x63,
	0x65, 0x49, 0x64, 0x12, 0x17, 0x0a, 0x07, 0x73, 0x70, 0x61, 0x6e, 0x5f, 0x69, 0x64, 0x18, 0x02,
	0x20, 0x02, 0x28, 0x06, 0x52, 0x06, 0x73, 0x70, 0x61, 0x6e, 0x49, 0x64, 0x12, 0x24, 0x0a, 0x0e,
	0x70, 0x61, 0x72, 0x65, 0x6e, 0x74, 0x5f, 0x73, 0x70, 0x61, 0x6e, 0x5f, 0x69, 0x64, 0x18, 0x03,
	0x20, 0x01, 0x28, 0x06, 0x52, 0x0c, 0x70, 0x61, 0x72, 0x65, 0x6e, 0x74, 0x53, 0x70, 0x61, 0x6e,
	0x49, 0x64, 0x12, 0x2f, 0x0a, 0x14, 0x66, 0x6f, 0x6c, 0x6c, 0x6f, 0x77, 0x73, 0x5f, 0x66, 0x72,
	0x6f, 0x6d, 0x5f, 0x73, 0x70, 0x61, 0x6e, 0x5f, 0x69, 0x64, 0x18, 0x04, 0x20, 0x01, 0x28, 0x06,
	0x52, 0x11, 0x66, 0x6f, 0x6c, 0x6c, 0x6f, 0x77, 0x73, 0x46, 0x72, 0x6f, 0x6d, 0x53, 0x70, 0x61,
	0x6e, 0x49, 0x64, 0x12, 0x12, 0x0a, 0x04, 0x6e, 0x61, 0x6d, 0x65, 0x18, 0x05, 0x20, 0x02, 0x28,
	0x09, 0x52, 0x04, 0x6e, 0x61, 0x6d, 0x65, 0x12, 0x1d, 0x0a, 0x0a, 0x73, 0x74, 0x61, 0x72, 0x74,
	0x5f, 0x74, 0x69, 0x6d, 0x65, 0x18, 0x06, 0x20, 0x02, 0x28, 0x06, 0x52, 0x09, 0x73, 0x74, 0x61,
	0x72, 0x74, 0x54, 0x69, 0x6d, 0x65, 0x12, 0x1a, 0x0a, 0x08, 0x64, 0x75, 0x72, 0x61, 0x74, 0x69,
	0x6f, 0x6e, 0x18, 0x07, 0x20, 0x02, 0x28, 0x06, 0x52, 0x08, 0x64, 0x75, 0x72, 0x61, 0x74, 0x69,
	0x6f, 0x6e, 0x12, 0x2d, 0x0a, 0x04, 0x74, 0x61, 0x67, 0x73, 0x18, 0x08, 0x20, 0x03, 0x28, 0x0b,
	0x32, 0x19, 0x2e, 0x4e, 0x59, 0x54, 0x2e, 0x4e, 0x54, 0x72, 0x61, 0x63, 0x69, 0x6e, 0x67, 0x2e,
	0x4e, 0x50, 0x72, 0x6f, 0x74, 0x6f, 0x2e, 0x54, 0x54, 0x61, 0x67, 0x52, 0x04, 0x74, 0x61, 0x67,
	0x73, 0x22, 0x3e, 0x0a, 0x0a, 0x54, 0x53, 0x70, 0x61, 0x6e, 0x42, 0x61, 0x74, 0x63, 0x68, 0x12,
	0x30, 0x0a, 0x05, 0x73, 0x70, 0x61, 0x6e, 0x73, 0x18, 0x01, 0x20, 0x03, 0x28, 0x0b, 0x32, 0x1a,
	0x2e, 0x4e, 0x59, 0x54, 0x2e, 0x4e, 0x54, 0x72, 0x61, 0x63, 0x69, 0x6e, 0x67, 0x2e, 0x4e, 0x50,
	0x72, 0x6f, 0x74, 0x6f, 0x2e, 0x54, 0x53, 0x70, 0x61, 0x6e, 0x52, 0x05, 0x73, 0x70, 0x61, 0x6e,
	0x73, 0x42, 0x44, 0x0a, 0x15, 0x74, 0x65, 0x63, 0x68, 0x2e, 0x79, 0x74, 0x73, 0x61, 0x75, 0x72,
	0x75, 0x73, 0x2e, 0x74, 0x72, 0x61, 0x63, 0x69, 0x6e, 0x67, 0x50, 0x01, 0x5a, 0x29, 0x61, 0x2e,
	0x79, 0x61, 0x6e, 0x64, 0x65, 0x78, 0x2d, 0x74, 0x65, 0x61, 0x6d, 0x2e, 0x72, 0x75, 0x2f, 0x79,
	0x74, 0x2f, 0x67, 0x6f, 0x2f, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x2f, 0x63, 0x6f, 0x72, 0x65, 0x2f,
	0x74, 0x72, 0x61, 0x63, 0x69, 0x6e, 0x67,
}

var (
	file_yt_yt_proto_yt_core_tracing_proto_span_proto_rawDescOnce sync.Once
	file_yt_yt_proto_yt_core_tracing_proto_span_proto_rawDescData = file_yt_yt_proto_yt_core_tracing_proto_span_proto_rawDesc
)

func file_yt_yt_proto_yt_core_tracing_proto_span_proto_rawDescGZIP() []byte {
	file_yt_yt_proto_yt_core_tracing_proto_span_proto_rawDescOnce.Do(func() {
		file_yt_yt_proto_yt_core_tracing_proto_span_proto_rawDescData = protoimpl.X.CompressGZIP(file_yt_yt_proto_yt_core_tracing_proto_span_proto_rawDescData)
	})
	return file_yt_yt_proto_yt_core_tracing_proto_span_proto_rawDescData
}

var file_yt_yt_proto_yt_core_tracing_proto_span_proto_msgTypes = make([]protoimpl.MessageInfo, 3)
var file_yt_yt_proto_yt_core_tracing_proto_span_proto_goTypes = []interface{}{
	(*TTag)(nil),       // 0: NYT.NTracing.NProto.TTag
	(*TSpan)(nil),      // 1: NYT.NTracing.NProto.TSpan
	(*TSpanBatch)(nil), // 2: NYT.NTracing.NProto.TSpanBatch
	(*misc.TGuid)(nil), // 3: NYT.NProto.TGuid
}
var file_yt_yt_proto_yt_core_tracing_proto_span_proto_depIdxs = []int32{
	3, // 0: NYT.NTracing.NProto.TSpan.trace_id:type_name -> NYT.NProto.TGuid
	0, // 1: NYT.NTracing.NProto.TSpan.tags:type_name -> NYT.NTracing.NProto.TTag
	1, // 2: NYT.NTracing.NProto.TSpanBatch.spans:type_name -> NYT.NTracing.NProto.TSpan
	3, // [3:3] is the sub-list for method output_type
	3, // [3:3] is the sub-list for method input_type
	3, // [3:3] is the sub-list for extension type_name
	3, // [3:3] is the sub-list for extension extendee
	0, // [0:3] is the sub-list for field type_name
}

func init() { file_yt_yt_proto_yt_core_tracing_proto_span_proto_init() }
func file_yt_yt_proto_yt_core_tracing_proto_span_proto_init() {
	if File_yt_yt_proto_yt_core_tracing_proto_span_proto != nil {
		return
	}
	if !protoimpl.UnsafeEnabled {
		file_yt_yt_proto_yt_core_tracing_proto_span_proto_msgTypes[0].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TTag); i {
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
		file_yt_yt_proto_yt_core_tracing_proto_span_proto_msgTypes[1].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TSpan); i {
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
		file_yt_yt_proto_yt_core_tracing_proto_span_proto_msgTypes[2].Exporter = func(v interface{}, i int) interface{} {
			switch v := v.(*TSpanBatch); i {
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
			RawDescriptor: file_yt_yt_proto_yt_core_tracing_proto_span_proto_rawDesc,
			NumEnums:      0,
			NumMessages:   3,
			NumExtensions: 0,
			NumServices:   0,
		},
		GoTypes:           file_yt_yt_proto_yt_core_tracing_proto_span_proto_goTypes,
		DependencyIndexes: file_yt_yt_proto_yt_core_tracing_proto_span_proto_depIdxs,
		MessageInfos:      file_yt_yt_proto_yt_core_tracing_proto_span_proto_msgTypes,
	}.Build()
	File_yt_yt_proto_yt_core_tracing_proto_span_proto = out.File
	file_yt_yt_proto_yt_core_tracing_proto_span_proto_rawDesc = nil
	file_yt_yt_proto_yt_core_tracing_proto_span_proto_goTypes = nil
	file_yt_yt_proto_yt_core_tracing_proto_span_proto_depIdxs = nil
}
