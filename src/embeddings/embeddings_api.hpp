//*****************************************************************************
// Copyright 2024 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//*****************************************************************************
#pragma once

#include <string>
#include <variant>
#include <vector>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include "mediapipe/framework/port/canonical_errors.h"
#pragma GCC diagnostic pop

#include "rapidjson/document.h"

enum class EncodingFormat {
    FLOAT,
    BASE64
};

struct EmbeddingsRequest {
    std::variant<std::vector<std::string>, std::vector<std::vector<int>>> input;
    EncodingFormat encoding_format;

    static std::variant<EmbeddingsRequest, std::string> fromJson(rapidjson::Document* request);
};

class EmbeddingsHandler {
    rapidjson::Document& doc;
    EmbeddingsRequest request;

public:
    EmbeddingsHandler(rapidjson::Document& document) :
        doc(document) {}

    std::variant<std::vector<std::string>, std::vector<std::vector<int>>>& getInput();
    EncodingFormat getEncodingFormat() const;

    absl::Status parseRequest();
};