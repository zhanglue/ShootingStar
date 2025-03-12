cc_binary(
    name = "greet_server",
    srcs = ["greet_server.cc"],
    defines = ["BAZEL_BUILD"],
    deps = [
        "//src/Protos:greet_cc_grpc",
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_grpc_grpc//:grpc++_reflection",
        "@com_github_abseil_abseil_cpp//absl/flags:flag",
        "@com_github_abseil_abseil_cpp//absl/flags:parse",
        "@com_github_abseil_abseil_cpp//absl/strings:str_format",
    ],
)
