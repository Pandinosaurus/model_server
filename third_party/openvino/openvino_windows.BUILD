#
# Copyright (c) 2024 Intel Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

package(
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "openvino_new_headers",
    hdrs = glob([
        "include/openvino/**/*.*"
    ]),
    strip_include_prefix = "include",
    visibility = ["//visibility:public"],
)

cc_library(
    name = "openvino",
    srcs = glob([
        "lib\\intel64\\Release\\openvino.lib"
    ]),
    #strip_include_prefix = "include/ie",
    visibility = ["//visibility:public"],
    deps = [
        ":openvino_new_headers",
        "@windows_opencl//:opencl",
        "@windows_opencl2//:opencl2",
    ],
    defines = ["OV_GPU_USE_OPENCL_HPP"],
    # TODO: Include headers when enabling OCL/VAAPI
    # copts = ["-I./"],
    # linkopts = ["-lopenvino"],
)
