find_package(Boost 1.88.0 EXACT REQUIRED PATHS ${ERCAT_THIRDPARTY_PATH})
find_package(absl REQUIRED PATHS ${ERCAT_THIRDPARTY_PATH})
find_package(utf8_range REQUIRED PATHS ${ERCAT_THIRDPARTY_PATH})
find_package(Protobuf CONFIG REQUIRED PATHS ${ERCAT_THIRDPARTY_PATH})
find_package(gRPC CONFIG REQUIRED PATHS ${ERCAT_THIRDPARTY_PATH})

# Paths for generated files
set(proto_srcs "${CMAKE_CURRENT_SOURCE_DIR}/catalog.pb.cc")
set(proto_hdrs "${CMAKE_CURRENT_SOURCE_DIR}/catalog.pb.h")
set(grpc_srcs "${CMAKE_CURRENT_SOURCE_DIR}/catalog.grpc.pb.cc")
set(grpc_hdrs "${CMAKE_CURRENT_SOURCE_DIR}/catalog.grpc.pb.h")

# Get the paths to the compilers
find_program(PROTOC_PATH protoc HINTS ${ERCAT_THIRDPARTY_PATH}/bin)
find_program(GRPC_CPP_PLUGIN_PATH grpc_cpp_plugin HINTS ${ERCAT_THIRDPARTY_PATH}/bin)

# Custom command to run protoc for both protobuf and grpc
add_custom_command(
      OUTPUT "${proto_srcs}" "${proto_hdrs}" "${grpc_srcs}" "${grpc_hdrs}"
      COMMAND ${PROTOC_PATH}
      ARGS --cpp_out "${CMAKE_CURRENT_SOURCE_DIR}"
           --grpc_out "${CMAKE_CURRENT_SOURCE_DIR}"
           --plugin=protoc-gen-grpc="${GRPC_CPP_PLUGIN_PATH}"
           -I "${CMAKE_CURRENT_SOURCE_DIR}"
           "${CMAKE_CURRENT_SOURCE_DIR}/catalog.proto"
      DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/catalog.proto"
)

# 1. ADD THIS: Create a global target that triggers the custom command above
add_custom_target(catalog_proto_gen
      DEPENDS "${proto_srcs}" "${proto_hdrs}" "${grpc_srcs}" "${grpc_hdrs}"
)

add_library(grpc STATIC grpcserver.cpp ${proto_srcs} ${grpc_srcs})

# 2. ADD THIS: Tell the grpc library it depends on this generation step
add_dependencies(grpc catalog_proto_gen)

target_include_directories(grpc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../.. )
target_include_directories(grpc PUBLIC ${ERCAT_THIRDPARTY_INCLUDE})

target_link_libraries(grpc PRIVATE Boost::headers protobuf::libprotobuf gRPC::grpc++_unsecure)
target_link_libraries(grpc PUBLIC utils translator service)




# # Include generated headers
# include_directories("${CMAKE_CURRENT_BINARY_DIR}")

# # Define the Server
# add_executable(grpc_server server.cpp ${proto_srcs} ${grpc_srcs})
# target_link_libraries(grpc_server 
#     protobuf::libprotobuf 
#     gRPC::grpc++
# )

# # Define the Client
# add_executable(grpc_client client.cpp ${proto_srcs} ${grpc_srcs})
# target_link_libraries(grpc_client 
#     protobuf::libprotobuf 
#     gRPC::grpc++
# )


# target_include_directories(protocol
#   PUBLIC
#   .
#   ${CMAKE_CURRENT_BINARY_DIR}
#   ${Protobuf_INCLUDE_DIRS}
# )
# find_package(Protobuf REQUIRED)
# find_package(gRPC CONFIG REQUIRED)
# target_link_libraries(protocol ${Protobuf_LIBRARIES})
# get_target_property(grpc_cpp_plugin_location gRPC::grpc_cpp_plugin LOCATION)

# protobuf_generate( TARGET protocol LANGUAGE CPP PROTOS msg.proto )
# protobuf_generate( 
#     TARGET
#         protocol 
#     LANGUAGE
#         grpc 
#     PROTOS 
#         myrpc.proto 
#     PLUGIN
#         "protoc-gen-grpc=${grpc_cpp_plugin_location}"
# )

# ../../thirdparty/install/bin/protoc -I=. --grpc_out=. --plugin=protoc-gen-grpc=../../thirdparty/install/bin/grpc_cpp_plugin ./catalog.proto
# ../../thirdparty/install/bin/protoc -I=. --cpp_out=. ./catalog.proto