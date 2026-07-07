#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "btos/exec/slippage.hpp"
#include "btos/plugin_abi.h"
#include "btos/portfolio/commission.hpp"

using namespace std;

namespace btos {

class PluginRegistry {
  public:
    PluginRegistry() = default;
    ~PluginRegistry();
    PluginRegistry(const PluginRegistry&) = delete;
    PluginRegistry& operator=(const PluginRegistry&) = delete;

    void load_shared_library(const std::string& so_path);

    void register_manifest(const btos_plugin_manifest* manifest);

    [[nodiscard]] std::unique_ptr<ISlippageModel> create_slippage(const std::string& name,
                                                                  const std::string& json_config) const;

    [[nodiscard]] std::unique_ptr<ICommissionModel> create_commission(const std::string& name,
                                                                      const std::string& json_config) const;

    [[nodiscard]] bool has(const std::string& name) const { return manifests_.count(name) > 0; }

  private:
    std::unordered_map<std::string, const btos_plugin_manifest*> manifests_;
    std::vector<void*> handles_;
};

}
