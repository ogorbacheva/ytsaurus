/*
 *
 * Copyright 2015 gRPC authors.
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
 *
 */

#ifndef GRPC_INTERNAL_COMPILER_PROTOBUF_PLUGIN_H
#define GRPC_INTERNAL_COMPILER_PROTOBUF_PLUGIN_H

#include <vector>

#include "src/compiler/config.h"
#include "src/compiler/cpp_generator_helpers.h"
#include "src/compiler/python_generator_helpers.h"
#include "src/compiler/python_private_generator.h"
#include "src/compiler/schema_interface.h"

// Get leading or trailing comments in a string.
template <typename DescriptorType>
inline TString GetCommentsHelper(const DescriptorType* desc, bool leading,
                                     const TString& prefix) {
  return grpc_generator::GetPrefixedComments(desc, leading, prefix);
}

class ProtoBufMethod : public grpc_generator::Method {
 public:
  ProtoBufMethod(const grpc::protobuf::MethodDescriptor* method)
      : method_(method) {}

  TString name() const { return method_->name(); }

  TString input_type_name() const {
    return grpc_cpp_generator::ClassName(method_->input_type(), true);
  }
  TString output_type_name() const {
    return grpc_cpp_generator::ClassName(method_->output_type(), true);
  }

  TString get_input_type_name() const {
    return method_->input_type()->file()->name();
  }
  TString get_output_type_name() const {
    return method_->output_type()->file()->name();
  }

  // TODO(https://github.com/grpc/grpc/issues/18800): Clean this up.
  bool get_module_and_message_path_input(
      TString* str, TString generator_file_name,
      bool generate_in_pb2_grpc, TString import_prefix,
      const std::vector<TString>& prefixes_to_filter) const final {
    return grpc_python_generator::GetModuleAndMessagePath(
        method_->input_type(), str, generator_file_name, generate_in_pb2_grpc,
        import_prefix, prefixes_to_filter);
  }

  bool get_module_and_message_path_output(
      TString* str, TString generator_file_name,
      bool generate_in_pb2_grpc, TString import_prefix,
      const std::vector<TString>& prefixes_to_filter) const final {
    return grpc_python_generator::GetModuleAndMessagePath(
        method_->output_type(), str, generator_file_name, generate_in_pb2_grpc,
        import_prefix, prefixes_to_filter);
  }

  bool NoStreaming() const {
    return !method_->client_streaming() && !method_->server_streaming();
  }

  bool ClientStreaming() const { return method_->client_streaming(); }

  bool ServerStreaming() const { return method_->server_streaming(); }

  bool BidiStreaming() const {
    return method_->client_streaming() && method_->server_streaming();
  }

  TString GetLeadingComments(const TString prefix) const {
    return GetCommentsHelper(method_, true, prefix);
  }

  TString GetTrailingComments(const TString prefix) const {
    return GetCommentsHelper(method_, false, prefix);
  }

  vector<TString> GetAllComments() const {
    return grpc_python_generator::get_all_comments(method_);
  }

 private:
  const grpc::protobuf::MethodDescriptor* method_;
};

class ProtoBufService : public grpc_generator::Service {
 public:
  ProtoBufService(const grpc::protobuf::ServiceDescriptor* service)
      : service_(service) {}

  TString name() const { return service_->name(); }

  int method_count() const { return service_->method_count(); }
  std::unique_ptr<const grpc_generator::Method> method(int i) const {
    return std::unique_ptr<const grpc_generator::Method>(
        new ProtoBufMethod(service_->method(i)));
  }

  TString GetLeadingComments(const TString prefix) const {
    return GetCommentsHelper(service_, true, prefix);
  }

  TString GetTrailingComments(const TString prefix) const {
    return GetCommentsHelper(service_, false, prefix);
  }

  vector<TString> GetAllComments() const {
    return grpc_python_generator::get_all_comments(service_);
  }

 private:
  const grpc::protobuf::ServiceDescriptor* service_;
};

class ProtoBufPrinter : public grpc_generator::Printer {
 public:
  ProtoBufPrinter(TString* str)
      : output_stream_(str), printer_(&output_stream_, '$') {}

  void Print(const std::map<TString, TString>& vars,
             const char* string_template) {
    printer_.Print(vars, string_template);
  }

  void Print(const char* string) { printer_.Print(string); }
  void PrintRaw(const char* string) { printer_.PrintRaw(string); }
  void Indent() { printer_.Indent(); }
  void Outdent() { printer_.Outdent(); }

 private:
  grpc::protobuf::io::StringOutputStream output_stream_;
  grpc::protobuf::io::Printer printer_;
};

class ProtoBufFile : public grpc_generator::File {
 public:
  ProtoBufFile(const grpc::protobuf::FileDescriptor* file) : file_(file) {}

  TString filename() const { return file_->name(); }
  TString filename_without_ext() const {
    return grpc_generator::StripProto(filename());
  }

  TString package() const { return file_->package(); }
  std::vector<TString> package_parts() const {
    return grpc_generator::tokenize(package(), ".");
  }

  TString additional_headers() const { return ""; }

  int service_count() const { return file_->service_count(); }
  std::unique_ptr<const grpc_generator::Service> service(int i) const {
    return std::unique_ptr<const grpc_generator::Service>(
        new ProtoBufService(file_->service(i)));
  }

  std::unique_ptr<grpc_generator::Printer> CreatePrinter(
      TString* str) const {
    return std::unique_ptr<grpc_generator::Printer>(new ProtoBufPrinter(str));
  }

  TString GetLeadingComments(const TString prefix) const {
    return GetCommentsHelper(file_, true, prefix);
  }

  TString GetTrailingComments(const TString prefix) const {
    return GetCommentsHelper(file_, false, prefix);
  }

  vector<TString> GetAllComments() const {
    return grpc_python_generator::get_all_comments(file_);
  }

  vector<TString> GetImportNames() const {
    vector<TString> proto_names;
    for (int i = 0; i < file_->dependency_count(); ++i) {
      const auto& dep = *file_->dependency(i);
      proto_names.push_back(dep.name());
    }
    return proto_names;
  }

 private:
  const grpc::protobuf::FileDescriptor* file_;
};

#endif  // GRPC_INTERNAL_COMPILER_PROTOBUF_PLUGIN_H
