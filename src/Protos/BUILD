package(default_visibility = ["//visibility:public"])

load("@com_github_protocolbuffers_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@com_github_grpc_grpc//bazel:cc_grpc_library.bzl", "cc_grpc_library")
load("@rules_proto//proto:defs.bzl", "proto_library")

proto_library(
    name = "greet_proto",
    srcs = ["greet.proto"],
)

cc_proto_library(
    name = "greet_cc_proto",
    deps = [":greet_proto"],
)

cc_grpc_library(
    name = "greet_cc_grpc",
    srcs = ["greet_proto"],
    grpc_only = True,
    deps = [":greet_cc_proto"],
)
