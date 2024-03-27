PY2_LIBRARY()

LICENSE(BSD-3-Clause)

LICENSE_TEXTS(.yandex_meta/licenses.list.txt)

VERSION(3.17.3)

ORIGINAL_SOURCE(https://github.com/protocolbuffers/protobuf/archive/v3.17.3.tar.gz)

PEERDIR(
    contrib/libs/protobuf_old
    contrib/libs/protobuf_old/builtin_proto/protos_from_protobuf
    #contrib/libs/protobuf_old/builtin_proto/protos_from_protoc
    contrib/python/six
)

ADDINCL(
    contrib/python/protobuf/py2
    contrib/libs/protobuf_old
    contrib/libs/protobuf_old/src
)

NO_COMPILER_WARNINGS()

NO_LINT()

PY_REGISTER(
    google.protobuf.internal._api_implementation
    google.protobuf.pyext._message
)

PY_SRCS(
    TOP_LEVEL
    google/__init__.py
    google/protobuf/__init__.py
    google/protobuf/compiler/__init__.py
    google/protobuf/descriptor.py
    google/protobuf/descriptor_database.py
    google/protobuf/descriptor_pool.py
    google/protobuf/internal/__init__.py
    google/protobuf/internal/_parameterized.py
    google/protobuf/internal/api_implementation.py
    google/protobuf/internal/containers.py
    google/protobuf/internal/decoder.py
    google/protobuf/internal/encoder.py
    google/protobuf/internal/enum_type_wrapper.py
    google/protobuf/internal/extension_dict.py
    google/protobuf/internal/message_listener.py
    google/protobuf/internal/python_message.py
    google/protobuf/internal/type_checkers.py
    google/protobuf/internal/well_known_types.py
    google/protobuf/internal/wire_format.py
    google/protobuf/json_format.py
    google/protobuf/message.py
    google/protobuf/message_factory.py
    google/protobuf/proto_builder.py
    google/protobuf/pyext/__init__.py
    google/protobuf/pyext/cpp_message.py
    google/protobuf/reflection.py
    google/protobuf/service.py
    google/protobuf/service_reflection.py
    google/protobuf/symbol_database.py
    google/protobuf/text_encoding.py
    google/protobuf/text_format.py
    google/protobuf/util/__init__.py
)

CFLAGS(
    -DPYTHON_PROTO2_CPP_IMPL_V2
)

SRCS(
    google/protobuf/internal/api_implementation.cc
    google/protobuf/pyext/descriptor.cc
    google/protobuf/pyext/descriptor_containers.cc
    google/protobuf/pyext/descriptor_database.cc
    google/protobuf/pyext/descriptor_pool.cc
    google/protobuf/pyext/extension_dict.cc
    google/protobuf/pyext/field.cc
    google/protobuf/pyext/map_container.cc
    google/protobuf/pyext/message.cc
    google/protobuf/pyext/message_factory.cc
    google/protobuf/pyext/message_module.cc
    google/protobuf/pyext/repeated_composite_container.cc
    google/protobuf/pyext/repeated_scalar_container.cc
    google/protobuf/pyext/unknown_fields.cc
)

END()
