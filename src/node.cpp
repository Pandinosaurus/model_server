//*****************************************************************************
// Copyright 2020 Intel Corporation
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
#include "node.hpp"

#include <algorithm>
#include <sstream>

#include <spdlog/spdlog.h>

#include "nodesession.hpp"
#include "status.hpp"

namespace ovms {

Node::Node(const std::string& nodeName) :
    nodeName(nodeName) {
}
Status Node::fetchResults(session_key_t sessionId, std::unordered_map<session_key_t, std::pair<NodeSessionMetadata, BlobMap>>& nodeSessionOutputs) {
    auto it = nodeSessions.find(sessionId);
    auto& nodeSession = it->second;
    if (it == nodeSessions.end()) {
    // TODO error handle
        return StatusCode::UNKNOWN_ERROR;
    }
    return fetchResults(*nodeSession, nodeSessionOutputs);
}

Status Node::fetchResults(NodeSession& nodeSession, std::unordered_map<session_key_t, std::pair<NodeSessionMetadata, BlobMap>>& nodeSessionOutputs) { return StatusCode::OK;} // TODO remove

void Node::printNodeConnections(const std::string& nodeName, const std::string& sourceNode, const InputPairs& pairs) {
    std::stringstream ss;
    ss << "Links from:" << sourceNode << " to:" << nodeName << ":\n";
    for (auto& pair : pairs) {
        ss << "\t" << nodeName << "[" << pair.second << "]=" << sourceNode << "[" << pair.first << "]\n";
    }
    SPDLOG_DEBUG(ss.str());
}

Status Node::setInputs(const Node& dependency, BlobMap& inputs) {
    // mapping for dependency - keeps mapping between dependency output name and this node input name
    const auto& mapping_for_dependency = this->getMappingByDependency(dependency);

    // assign all input blobs from inputs that are required by this node for future inference
    for (const auto& pair : mapping_for_dependency) {
        const auto& dependency_output_name = pair.first;
        const auto& current_node_input_name = pair.second;

        // possibly incorrectly constructed pipeline - required input missing from previous node
        auto it = inputs.find(dependency_output_name);
        if (it == inputs.end()) {
            SPDLOG_WARN("Node::setInputs: error setting required input for (Node name {}) from (Node name {}): dependency is missing output name {}",
                getName(),
                dependency.getName(),
                dependency_output_name);
            return StatusCode::INVALID_MISSING_INPUT;
        }
        SPDLOG_DEBUG("Node::setInputs: setting required input for (Node name {}) from (Node name {}), input name: {}, dependency output name: {}",
            getName(),
            dependency.getName(),
            current_node_input_name,
            dependency_output_name);
        this->inputBlobs[current_node_input_name] = it->second;
        NodeSessionMetadata metadata;
        getNodeSession(metadata).setInput(current_node_input_name, it->second);
    }

    finishedDependenciesCount++;
    return StatusCode::OK;
}

NodeSession& Node::getNodeSession(const NodeSessionMetadata& metadata) {
    auto it = nodeSessions.find(metadata.getSessionKey());
    if (it != nodeSessions.end()) {
        return *(*it).second;
    }
    auto emplacePair = nodeSessions.emplace(metadata.getSessionKey(), createNodeSession(metadata));
    return *(*(emplacePair.first)).second;
}

std::unique_ptr<NodeSession> Node::createNodeSession(const NodeSessionMetadata& metadata) {
    return std::make_unique<NodeSession>(metadata);
}

}  // namespace ovms
