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
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <stdlib.h>

#include "../get_model_metadata_impl.hpp"
#include "../processing_spec.hpp"
#include "../statefulmodelinstance.hpp"
#include "test_utils.hpp"

using testing::Return;

namespace {

static const char* modelStatefulConfig = R"(
{
    "model_config_list": [
        {
            "config": {
                "name": "dummy",
                "base_path": "/ovms/src/test/dummy",
                "target_device": "CPU",
                "model_version_policy": {"latest": {"num_versions":1}},
                "nireq": 100,
                "stateful": true,
                "low_latency_transformation": true,
                "sequence_timeout_seconds": 120,
                "max_sequence_number": 1000,
                "shape": {"b": "(1,10) "}
            }
        }
    ]
})";

constexpr const char* DUMMY_MODEL_INPUT_NAME = "b";
class StatefulModelInstance : public TestWithTempDir {
public:
    std::string configFilePath;
    std::string ovmsConfig;
    std::string modelPath;
    std::string dummyModelName;
    inputs_info_t modelInput;
    std::pair<std::string, std::tuple<ovms::shape_t, tensorflow::DataType>> sequenceId;
    std::pair<std::string, std::tuple<ovms::shape_t, tensorflow::DataType>> sequenceControlStart;

    void SetUpConfig(const std::string& configContent) {
        ovmsConfig = configContent;
        dummyModelName = "dummy";
        const std::string modelPathToReplace{"/ovms/src/test/dummy"};
        ovmsConfig.replace(ovmsConfig.find(modelPathToReplace), modelPathToReplace.size(), modelPath);
        configFilePath = directoryPath + "/ovms_config.json";
    }
    void SetUp() override {
        TestWithTempDir::SetUp();
        // Prepare manager
        modelPath = directoryPath + "/dummy/";
        SetUpConfig(modelStatefulConfig);
        std::filesystem::copy("/ovms/src/test/dummy", modelPath, std::filesystem::copy_options::recursive);
        modelInput = {{DUMMY_MODEL_INPUT_NAME,
            std::tuple<ovms::shape_t, tensorflow::DataType>{{1, 10}, tensorflow::DataType::DT_FLOAT}}};
    }

    void TearDown() override {
        TestWithTempDir::TearDown();
        modelInput.clear();
    }
};

TEST_F(StatefulModelInstance, positiveValidate) {
    ConstructorEnabledModelManager manager;
    createConfigFileWithContent(ovmsConfig, configFilePath);
    auto status = manager.loadConfig(configFilePath);
    ASSERT_TRUE(status.ok());
    ovms::ProcessingSpec spec = ovms::ProcessingSpec();
    auto modelInstance = manager.findModelInstance(dummyModelName);
    uint64_t seqId = 1;
    tensorflow::serving::PredictRequest request = preparePredictRequest(modelInput);
    setRequestSequenceId(&request, seqId);
    setRequestSequenceControl(&request, SEQUENCE_START);

    status = modelInstance->validate(&request, &spec);
    ASSERT_TRUE(status.ok());

    request = preparePredictRequest(modelInput);
    setRequestSequenceId(&request, seqId);
    setRequestSequenceControl(&request, SEQUENCE_END);

    status = modelInstance->validate(&request, &spec);
    ASSERT_TRUE(status.ok());

    request = preparePredictRequest(modelInput);
    setRequestSequenceId(&request, seqId);
    setRequestSequenceControl(&request, NO_CONTROL_INPUT);

    status = modelInstance->validate(&request, &spec);
    ASSERT_TRUE(status.ok());
}

TEST_F(StatefulModelInstance, missingSeqId) {
    ConstructorEnabledModelManager manager;
    createConfigFileWithContent(ovmsConfig, configFilePath);
    auto status = manager.loadConfig(configFilePath);
    ASSERT_TRUE(status.ok());
    ovms::ProcessingSpec spec = ovms::ProcessingSpec();
    auto modelInstance = manager.findModelInstance(dummyModelName);
    tensorflow::serving::PredictRequest request = preparePredictRequest(modelInput);
    setRequestSequenceControl(&request, SEQUENCE_END);

    status = modelInstance->validate(&request, &spec);
    ASSERT_EQ(status.getCode(), ovms::StatusCode::SEQUENCE_ID_NOT_PROVIDED);
}

TEST_F(StatefulModelInstance, wrongSeqIdEnd) {
    ConstructorEnabledModelManager manager;
    createConfigFileWithContent(ovmsConfig, configFilePath);
    auto status = manager.loadConfig(configFilePath);
    ASSERT_TRUE(status.ok());
    ovms::ProcessingSpec spec = ovms::ProcessingSpec();
    auto modelInstance = manager.findModelInstance(dummyModelName);
    tensorflow::serving::PredictRequest request = preparePredictRequest(modelInput);
    setRequestSequenceControl(&request, SEQUENCE_END);

    uint64_t seqId = 0;
    setRequestSequenceId(&request, seqId);
    status = modelInstance->validate(&request, &spec);
    ASSERT_EQ(status.getCode(), ovms::StatusCode::SEQUENCE_ID_NOT_PROVIDED);
}

TEST_F(StatefulModelInstance, wrongSeqIdNoControl) {
    ConstructorEnabledModelManager manager;
    createConfigFileWithContent(ovmsConfig, configFilePath);
    auto status = manager.loadConfig(configFilePath);
    ASSERT_TRUE(status.ok());
    ovms::ProcessingSpec spec = ovms::ProcessingSpec();
    auto modelInstance = manager.findModelInstance(dummyModelName);
    tensorflow::serving::PredictRequest request = preparePredictRequest(modelInput);
    setRequestSequenceControl(&request, NO_CONTROL_INPUT);

    uint64_t seqId = 0;
    setRequestSequenceId(&request, seqId);
    status = modelInstance->validate(&request, &spec);
    ASSERT_EQ(status.getCode(), ovms::StatusCode::SEQUENCE_ID_NOT_PROVIDED);
}

TEST_F(StatefulModelInstance, wrongProtoKeywords) {
    ConstructorEnabledModelManager manager;
    createConfigFileWithContent(ovmsConfig, configFilePath);
    auto status = manager.loadConfig(configFilePath);
    ASSERT_TRUE(status.ok());
    ovms::ProcessingSpec spec = ovms::ProcessingSpec();
    auto modelInstance = manager.findModelInstance(dummyModelName);
    tensorflow::serving::PredictRequest request = preparePredictRequest(modelInput);
    auto& input = (*request.mutable_inputs())["sequenceid"];
    input.add_uint64_val(12);
    status = modelInstance->validate(&request, &spec);
    ASSERT_EQ(status.getCode(), ovms::StatusCode::SEQUENCE_ID_NOT_PROVIDED);
}

TEST_F(StatefulModelInstance, badControlInput) {
    ConstructorEnabledModelManager manager;
    createConfigFileWithContent(ovmsConfig, configFilePath);
    auto status = manager.loadConfig(configFilePath);
    ASSERT_TRUE(status.ok());
    ovms::ProcessingSpec spec = ovms::ProcessingSpec();
    auto modelInstance = manager.findModelInstance(dummyModelName);
    tensorflow::serving::PredictRequest request = preparePredictRequest(modelInput);
    request = preparePredictRequest(modelInput);
    auto& input = (*request.mutable_inputs())["sequence_control_input"];
    input.add_uint32_val(999);
    status = modelInstance->validate(&request, &spec);
    ASSERT_EQ(status.getCode(), ovms::StatusCode::INVALID_SEQUENCE_CONTROL_INPUT);
}

TEST_F(StatefulModelInstance, invalidProtoTypes) {
    ConstructorEnabledModelManager manager;
    createConfigFileWithContent(ovmsConfig, configFilePath);
    auto status = manager.loadConfig(configFilePath);
    ASSERT_TRUE(status.ok());
    ovms::ProcessingSpec spec = ovms::ProcessingSpec();
    auto modelInstance = manager.findModelInstance(dummyModelName);
    tensorflow::serving::PredictRequest request = preparePredictRequest(modelInput);
    auto& input = (*request.mutable_inputs())["sequence_id"];
    input.add_uint32_val(12);
    status = modelInstance->validate(&request, &spec);
    ASSERT_EQ(status.getCode(), ovms::StatusCode::SEQUENCE_ID_BAD_TYPE);

    request = preparePredictRequest(modelInput);
    input = (*request.mutable_inputs())["sequence_control_input"];
    input.add_uint64_val(1);
    status = modelInstance->validate(&request, &spec);
    ASSERT_EQ(status.getCode(), ovms::StatusCode::SEQUENCE_CONTROL_INPUT_BAD_TYPE);
}

}  // namespace
