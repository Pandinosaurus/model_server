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
#include <sys/stat.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <utility>
#include <vector>

#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>

#include <spdlog/spdlog.h>

#include "config.hpp"
#include "directoryversionreader.hpp"
#include "modelmanager.hpp"

namespace ovms {

const uint ModelManager::WATCHER_INTERVAL_SEC = 1;

Status ModelManager::start() {
    auto& config = ovms::Config::instance();
    // start manager using config file
    if (config.configPath() != "") {
        return start(config.configPath());
    }

    // start manager using commandline parameters
    ModelConfig modelConfig {
        config.modelName(),
        config.modelPath(),
        config.targetDevice(),
        config.batchSize(),
        config.nireq()
    };
    auto status = modelConfig.parsePluginConfig(config.pluginConfig());
    if (status != Status::OK) {
        spdlog::error("Couldn't parse plugin config: {}", StatusDescription::getError(status));
        return status;
    }

    status = modelConfig.parseModelVersionPolicy(config.modelVersionPolicy());
    if (status != Status::OK) {
        spdlog::error("Couldn't parse model version policy: {}", StatusDescription::getError(status));
        return status;
    }
    modelConfig.setBasePath(config.modelPath());
    return reloadModelWithVersions(modelConfig);
}

Status ModelManager::start(const std::string& jsonFilename) {
    Status s = loadConfig(jsonFilename);
    if (s != Status::OK) {
        return s;
    }
    if (!monitor.joinable()) {
        std::future<void> exitSignal = exit.get_future();
        std::thread t(std::thread(&ModelManager::watcher, this, std::move(exitSignal)));
        monitor = std::move(t);
        monitor.detach();
    }
    return Status::OK;
}

Status ModelManager::loadConfig(const std::string& jsonFilename) {
    spdlog::info("Loading configuration from {}", jsonFilename);

    rapidjson::Document doc;
    std::ifstream ifs(jsonFilename.c_str());
    // Perform some basic checks on the config file
    if (!ifs.good()) {
        spdlog::error("File is invalid {}", jsonFilename);
        return Status::FILE_INVALID;
    }

    rapidjson::IStreamWrapper isw(ifs);
    if (doc.ParseStream(isw).HasParseError()) {
        spdlog::error("Configuration file is not a valid JSON file.");
        return Status::JSON_INVALID;
    }

    // TODO validate json against schema
    const auto itr = doc.FindMember("model_config_list");
    if (itr == doc.MemberEnd() || !itr->value.IsArray()) {
        spdlog::error("Configuration file doesn't have models property.");
        return Status::JSON_INVALID;
    }

    // TODO reload model if no version change, just config change eg. CPU_STREAMS_THROUGHPUT
    models.clear();
    configFilename = jsonFilename;
    for (const auto& configs : itr->value.GetArray()) {
        ModelConfig modelConfig;
        modelConfig.parseNode(configs["config"]);
        reloadModelWithVersions(modelConfig);
    }
    return Status::OK;
}

void ModelManager::watcher(std::future<void> exit) {
    spdlog::info("Started config watcher thread");
    int64_t lastTime;
    struct stat statTime;

    stat(configFilename.c_str(), &statTime);
    lastTime = statTime.st_ctime;
    while (exit.wait_for(std::chrono::milliseconds(1)) == std::future_status::timeout) {
        std::this_thread::sleep_for(std::chrono::seconds(WATCHER_INTERVAL_SEC));
        stat(configFilename.c_str(), &statTime);
        if (lastTime != statTime.st_ctime) {
            lastTime = statTime.st_ctime;
            loadConfig(configFilename);
            spdlog::info("Model configuration changed");
        }
    }
    spdlog::info("Exited config watcher thread");
}

void ModelManager::join() {
    exit.set_value();
    if (monitor.joinable()) {
        monitor.join();
    }
}

std::shared_ptr<IVersionReader> ModelManager::getVersionReader(const std::string& path) {
    return std::make_shared<DirectoryVersionReader>(path);
}

void ModelManager::getVersionsToChange(
                        const std::map<model_version_t, std::shared_ptr<ModelInstance>>& modelVersionsInstances,
                        model_versions_t requestedVersions,
                        std::shared_ptr<model_versions_t>& versionsToStartIn,
                        std::shared_ptr<model_versions_t>& versionsToReloadIn,
                        std::shared_ptr<model_versions_t>& versionsToRetireIn) {
    std::sort(requestedVersions.begin(), requestedVersions.end());
    model_versions_t registeredModelVersions;
    spdlog::info("Currently registered versions count:{}", modelVersionsInstances.size());
    for (const auto& [version, versionInstance] : modelVersionsInstances) {
        spdlog::info("version:{} state:{}", version, ovms::ModelVersionStateToString(versionInstance->getStatus().getState()));
        registeredModelVersions.push_back(version);
    }

    model_versions_t alreadyRegisteredVersionsWhichAreRequested(requestedVersions.size());
    model_versions_t::iterator it = std::set_intersection(
        requestedVersions.begin(), requestedVersions.end(),
        registeredModelVersions.begin(), registeredModelVersions.end(),
        alreadyRegisteredVersionsWhichAreRequested.begin());
    alreadyRegisteredVersionsWhichAreRequested.resize(it - alreadyRegisteredVersionsWhichAreRequested.begin());

    std::shared_ptr<model_versions_t> versionsToReload = std::make_shared<model_versions_t>();
    for (const auto& version : alreadyRegisteredVersionsWhichAreRequested) {
        try {
            if (modelVersionsInstances.at(version)->getStatus().willEndUnloaded()) {
                versionsToReload->push_back(version);
            }
        } catch (std::out_of_range& e) {
            spdlog::error("Data race occured during versions update. Could not found version. Details:{}", e.what());
        }
    }

    std::shared_ptr<model_versions_t> versionsToRetire = std::make_shared<model_versions_t>(registeredModelVersions.size());
    it = std::set_difference(
        registeredModelVersions.begin(), registeredModelVersions.end(),
        requestedVersions.begin(), requestedVersions.end(),
        versionsToRetire->begin());
    versionsToRetire->resize(it - versionsToRetire->begin());
    try {
        it = std::remove_if(versionsToRetire->begin(),
                    versionsToRetire->end(),
                    [&modelVersionsInstances](model_version_t version) {
                        return modelVersionsInstances.at(version)->getStatus().willEndUnloaded();
                    });
    } catch (std::out_of_range& e) {
        spdlog::error("Data race occured during versions update. Could not found version. Details:{}", e.what());
    }
    versionsToRetire->resize(it - versionsToRetire->begin());

    std::shared_ptr<model_versions_t> versionsToStart = std::make_shared<model_versions_t>(requestedVersions.size());
    it = std::set_difference(
        requestedVersions.begin(), requestedVersions.end(),
        registeredModelVersions.begin(), registeredModelVersions.end(),
        versionsToStart->begin());
    versionsToStart->resize(it - versionsToStart->begin());

    versionsToStartIn = std::move(versionsToStart);
    versionsToReloadIn = std::move(versionsToReload);
    versionsToRetireIn = std::move(versionsToRetire);
}

Status ModelManager::addVersions(std::shared_ptr<ovms::Model> model, std::shared_ptr<model_versions_t> versionsToStart, ovms::ModelConfig &config) {
    Status status;
    for (const auto version : *versionsToStart) {
        spdlog::info("Will add model: {}; version: {} ...", config.getName(), version);
        config.setVersion(version);
        config.parseModelMapping();

        status = model->addVersion(config);
        if (status != Status::OK) {
            spdlog::error("Error occurred while loading model: {}; version: {}; error: {}",
                config.getName(),
                version,
                StatusDescription::getError(status));
            return status;
        }
    }
    return Status::OK;
}

Status ModelManager::retireVersions(std::shared_ptr<ovms::Model> model, std::shared_ptr<model_versions_t> versionsToRetire, ovms::ModelConfig &config) {
    Status status;
    for (const auto version : *versionsToRetire) {
        spdlog::info("Will unload model: {}; version: {} ...", config.getName(), version);
        auto modelVersion = model->getModelInstanceByVersion(version);
        if (!modelVersion) {
            status = Status::UNKNOWN_ERROR;
            spdlog::error("Error occurred while unloading model: {}; version: {}; error: {}",
                config.getName(),
                version,
                StatusDescription::getError(status));
            return status;
        }
        modelVersion->unloadModel();
        model->updateDefaultVersion();
    }
    return Status::OK;
}

Status ModelManager::reloadVersions(std::shared_ptr<ovms::Model> model, std::shared_ptr<model_versions_t> versionsToReload, ovms::ModelConfig &config) {
    Status status;
    for (const auto version : *versionsToReload) {
        spdlog::info("Will reload model: {}; version: {} ...", config.getName(), version);
        config.setVersion(version);
        status = config.parseModelMapping();
        if ((status != Status::OK) && (status != Status::FILE_INVALID)) {
            spdlog::error("Error while parsing model mapping for model {}", StatusDescription::getError(status));
        }

        auto modelVersion = model->getModelInstanceByVersion(version);
        if (!modelVersion) {
            spdlog::error("Error occurred while reloading model: {}; version: {}; error: {}",
                config.getName(),
                version,
                StatusDescription::getError(Status::UNKNOWN_ERROR));
            return Status::UNKNOWN_ERROR;
        }
        modelVersion->unloadModel();
        status = modelVersion->loadModel(config);
        if (status != Status::OK) {
            spdlog::error("Error occurred while loading model: {}; version: {}; error: {}",
                config.getName(),
                version,
                StatusDescription::getError(status));
            return status;
        }
        model->updateDefaultVersion();
    }
    return Status::OK;
}

std::shared_ptr<ovms::Model> ModelManager::getModelIfExistCreateElse(const std::string& modelName) {
    auto modelIt = models.find(modelName);
    if (models.end() == modelIt) {
        models[modelName] = modelFactory(modelName);
    }
    return models[modelName];
}

Status ModelManager::reloadModelWithVersions(ModelConfig& config) {
    std::vector<model_version_t> requestedVersions;
    std::shared_ptr<IVersionReader> versionReader = getVersionReader(config.getBasePath());
    Status status = versionReader->readAvailableVersions(requestedVersions);
    if (status != Status::OK) {
        return status;
    }
    requestedVersions = config.getModelVersionPolicy()->filter(requestedVersions);
    auto model = getModelIfExistCreateElse(config.getName());

    // TODO check if reload whole model when part of config changes (eg. CPU_THROUGHPUT_STREAMS)
    // right now assumes no need to reload model
    std::shared_ptr<model_versions_t> versionsToStart;
    std::shared_ptr<model_versions_t> versionsToReload;
    std::shared_ptr<model_versions_t> versionsToRetire;
    getVersionsToChange(model->getModelVersions(), requestedVersions, versionsToStart, versionsToReload, versionsToRetire);
    status = addVersions(model, versionsToStart, config);
    if (status != Status::OK) {
        spdlog::error("Error occurred while loading model {}; versions; error: {}",
            config.getName(),
            StatusDescription::getError(status));
        return status;
    }
    status = reloadVersions(model, versionsToReload, config);
    if (status != Status::OK) {
        spdlog::error("Error occurred while reloading model: {}; versions; error: {}",
            config.getName(),
            StatusDescription::getError(status));
        return status;
    }
    status = retireVersions(model, versionsToRetire, config);
    if (status != Status::OK) {
        spdlog::error("Error occurred while unloading model: {}; versions; error: {}",
            config.getName(),
            StatusDescription::getError(status));
        return status;
    }
    return Status::OK;
}

}  // namespace ovms