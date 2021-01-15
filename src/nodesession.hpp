//*****************************************************************************
// Copyright 2021 Intel Corporation
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

#include <memory>
#include <utility>

#include <inference_engine.hpp>

#include "nodesessionmetadata.hpp"

namespace ovms {
struct NodeInputHandler;
struct NodeOutputHandler;

class NodeSession {
    NodeSessionMetadata metadata;
    std::unique_ptr<NodeInputHandler> inputHandler;
    std::unique_ptr<NodeOutputHandler> outputHandler;

public:
    NodeSession(const NodeSessionMetadata& metadata);
    NodeSession(const NodeSessionMetadata&& metadata);
    virtual ~NodeSession();
    void setInput(const std::string& inputName, InferenceEngine::Blob::Ptr&);

    bool isReady() const;
};
}  // namespace ovms
