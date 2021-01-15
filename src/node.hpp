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
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <inference_engine.hpp>

#include "status.hpp"
#include "threadsafequeue.hpp"
#include "nodesession.hpp"

namespace ovms {

using BlobMap = std::unordered_map<std::string, InferenceEngine::Blob::Ptr>;
using BlobNames = std::vector<std::string>;
using InputPairs = std::vector<std::pair<std::string, std::string>>;
using session_key_t = std::string;

//class NodeSession;
//class NodeSessionMetadata;

class Node {
protected:
    std::string nodeName;

    std::vector<std::reference_wrapper<Node>> previous;
    std::vector<std::reference_wrapper<Node>> next;

    size_t finishedDependenciesCount = 0;

    // Blobs ready and waiting for execution
    std::unordered_map<session_key_t, std::unique_ptr<NodeSession>> nodeSessions;
    BlobMap inputBlobs;

    // Input/Output name mapping and list of required inputs from previous nodes
    std::unordered_map<std::string, InputPairs> blobNamesMapping;

public:
    Node(const std::string& nodeName);

    virtual ~Node() = default;

    const std::string& getName() const { return this->nodeName; }

    virtual Status execute(ThreadSafeQueue<std::reference_wrapper<Node>>& notifyEndQueue) = 0;
    virtual Status fetchResults(BlobMap& outputs) = 0;
    virtual Status fetchResults(session_key_t sessionId, std::unordered_map<session_key_t, std::pair<NodeSessionMetadata, BlobMap>>& nodeSessionOutputs);
protected:
    virtual Status fetchResults(NodeSession& nodeSession, std::unordered_map<session_key_t, std::pair<NodeSessionMetadata, BlobMap>>& nodeSessionOutputs);
public:

    Status setInputs(const Node& dependency, BlobMap& inputs);

    virtual void addDependency(Node& node, const InputPairs& blobNamesMapping) {
        this->previous.emplace_back(node);
        this->blobNamesMapping[node.getName()] = blobNamesMapping;
    }

    virtual void addDependant(Node& node) { this->next.emplace_back(node); }

    const InputPairs& getMappingByDependency(const Node& dependency) {
        return blobNamesMapping.at(dependency.getName());
    }
    bool isReady() const {
        return finishedDependenciesCount == previous.size();
    }
    const std::vector<std::reference_wrapper<Node>>& getNextNodes() {
        return next;
    }
    virtual void release() {}
    virtual bool tryDisarmStreamIdGuard(const uint microseconds = 1) { return true; }

    static void printNodeConnections(const std::string& nodeName, const std::string& sourceNode, const InputPairs& pairs);

protected:
    NodeSession& getNodeSession(const NodeSessionMetadata& metadata);
    virtual std::unique_ptr<NodeSession> createNodeSession(const NodeSessionMetadata& metadata);
};

}  // namespace ovms
