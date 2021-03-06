#include "app.h"

#include <string>
#include <filesystem>
#include <ranges>

#include <fmt/core.h>
#include <spdlog/spdlog.h>

#include "app_schema.h"
#include "environ.h"
#include "file_loading.h"
#include "utils.h"

namespace app {

namespace fs = std::filesystem;

extern const char *DEFAULT_ENV_FILEPATH = "./res/default_env.json";
extern const char *DEFAULT_APP_FILEPATH = "./res/default_app.json";
extern const char *DEFAULT_APPS_FILEPATH = "./res/apps.json";

// application
App::App() {
    m_parent_env = get_env();

    // load default app config
    auto app_doc_res = load_document_from_filename(DEFAULT_APP_FILEPATH);
    if (!app_doc_res) {
        m_runtime_errors.push_back(fmt::format("Failed to retrieve default app configuration file ({})", DEFAULT_APP_FILEPATH));
        return;
    }

    auto app_doc = std::move(app_doc_res.value());
    if (!validate_document(app_doc, DEFAULT_APP_SCHEMA)) {
        m_runtime_errors.push_back(std::string("Failed to validate default app config schema"));
        return;
    }

    auto default_app_cfg = load_app_config(app_doc);
    m_default_app_config = ManagedConfig(default_app_cfg);
}

App::App(const std::string &app_filepath)
: App() 
{
    open_app_config(app_filepath);
}

bool App::open_app_config(const std::string &app_filepath) {
    auto apps_doc_res = load_document_from_filename(app_filepath.c_str());
    if (!apps_doc_res) {
        m_runtime_warnings.push_back(fmt::format("Failed to read apps file ({})", app_filepath));
        return false;
    }

    auto apps_doc = std::move(apps_doc_res.value());
    if (!validate_document(apps_doc, APPS_SCHEMA)) {
        m_runtime_warnings.push_back(std::string("Failed to validate apps schema"));
        return false;
    }

    m_app_filepath = app_filepath;

    // load configuration into manager
    auto cfgs = load_app_configs(apps_doc);
    m_managed_configs.Clear();
    for (auto &cfg: cfgs) {
        m_managed_configs.Add(cfg);
    }
    m_managed_configs.ApplyChanges();

    // NOTE: we want backwards compatability with older configs
    // these don't have the current working directory stored, so we manually set it here
    for (auto &managed_cfg: m_managed_configs.GetConfigs()) {
        auto &cfg = managed_cfg->GetConfig();
        // ignore executable paths that aren't defined yet
        if (cfg.exec_path.length() == 0) {
            continue;
        }
        // only set if we don't have a cwd for an existing executable
        if (cfg.exec_cwd.length() == 0) {
            cfg.exec_cwd = fs::path(cfg.exec_path).remove_filename().string();
            managed_cfg->SetStatus(ManagedConfig::Status::CHANGED);
        }
    }

    return true;
}

void App::launch_app(AppConfig &app) {
    try {
        auto process_ptr = std::make_unique<AppProcess>(app, m_parent_env);
        m_processes.push_back(std::move(process_ptr));
    } catch (std::exception &ex) {
        m_runtime_warnings.push_back(ex.what());
    }
}

void App::save_configs() {
    if (!m_managed_configs.IsPendingSave()) {
        return;
    }

    auto &all_configs = m_managed_configs.GetConfigs();
    auto cfgs = all_configs | 
        std::views::filter([](std::shared_ptr<ManagedConfig> &cfg) {
            return !cfg->IsPendingDelete();
        }) |
        std::views::transform([](std::shared_ptr<ManagedConfig> &cfg) {
            return std::reference_wrapper(cfg->GetUnchangedConfig());
        });

    auto doc = create_app_configs_doc(cfgs);
    if (!write_document_to_file(m_app_filepath.c_str(), doc)) {
        m_runtime_warnings.push_back(fmt::format("Failed to save configs to {}", m_app_filepath));
    } else {
        m_managed_configs.CommitSave();
    }
}

}
