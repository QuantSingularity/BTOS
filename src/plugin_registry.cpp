#include "btos/plugin_registry.hpp"

#include <dlfcn.h>

#include <stdexcept>

using namespace std;

namespace btos {

namespace {

class AbiSlippage final : public ISlippageModel {
  public:
    AbiSlippage(const btos_plugin_manifest* m, void* self) : m_(m), self_(self) {}
    ~AbiSlippage() override { m_->destroy(self_); }
    AbiSlippage(const AbiSlippage&) = delete;
    AbiSlippage& operator=(const AbiSlippage&) = delete;
    double apply(const SlippageContext& ctx) const override {
        const auto* vt = static_cast<const btos_slippage_vtable*>(m_->vtable);
        return vt->apply(self_, ctx.reference_price, ctx.quantity, sign(ctx.side), ctx.adv,
                         ctx.sigma);
    }

  private:
    const btos_plugin_manifest* m_;
    void* self_;
};

class AbiCommission final : public ICommissionModel {
  public:
    AbiCommission(const btos_plugin_manifest* m, void* self) : m_(m), self_(self) {}
    ~AbiCommission() override { m_->destroy(self_); }
    AbiCommission(const AbiCommission&) = delete;
    AbiCommission& operator=(const AbiCommission&) = delete;
    double commission(double quantity, double price, double multiplier) const override {
        const auto* vt = static_cast<const btos_commission_vtable*>(m_->vtable);
        return vt->commission(self_, quantity, price, multiplier);
    }

  private:
    const btos_plugin_manifest* m_;
    void* self_;
};

}

PluginRegistry::~PluginRegistry() {
    for (void* h : handles_) dlclose(h);
}

void PluginRegistry::register_manifest(const btos_plugin_manifest* manifest) {
    if (manifest == nullptr) throw std::runtime_error("plugin: null manifest");
    if (manifest->abi_version != BTOS_PLUGIN_ABI_VERSION)
        throw std::runtime_error("plugin: ABI version mismatch");
    if (manifest->name == nullptr || manifest->create == nullptr || manifest->destroy == nullptr ||
        manifest->vtable == nullptr)
        throw std::runtime_error("plugin: incomplete manifest");
    manifests_[manifest->name] = manifest;
}

void PluginRegistry::load_shared_library(const std::string& so_path) {
    void* handle = dlopen(so_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr)
        throw std::runtime_error(std::string("plugin: dlopen failed: ") + dlerror());
    void* sym = dlsym(handle, "btos_plugin_manifest_v1");
    if (sym == nullptr) {
        dlclose(handle);
        throw std::runtime_error("plugin: missing btos_plugin_manifest_v1 in " + so_path);
    }
    const auto entry = reinterpret_cast<btos_plugin_entry_fn>(sym);
    try {
        register_manifest(entry());
    } catch (...) {
        dlclose(handle);
        throw;
    }
    handles_.push_back(handle);
}

std::unique_ptr<ISlippageModel> PluginRegistry::create_slippage(
    const std::string& name, const std::string& json_config) const {
    auto it = manifests_.find(name);
    if (it == manifests_.end()) throw std::runtime_error("plugin: unknown name " + name);
    if (it->second->kind != BTOS_PLUGIN_SLIPPAGE)
        throw std::runtime_error("plugin: " + name + " is not a slippage plugin");
    void* self = it->second->create(json_config.c_str());
    if (self == nullptr) throw std::runtime_error("plugin: create failed for " + name);
    return std::make_unique<AbiSlippage>(it->second, self);
}

std::unique_ptr<ICommissionModel> PluginRegistry::create_commission(
    const std::string& name, const std::string& json_config) const {
    auto it = manifests_.find(name);
    if (it == manifests_.end()) throw std::runtime_error("plugin: unknown name " + name);
    if (it->second->kind != BTOS_PLUGIN_COMMISSION)
        throw std::runtime_error("plugin: " + name + " is not a commission plugin");
    void* self = it->second->create(json_config.c_str());
    if (self == nullptr) throw std::runtime_error("plugin: create failed for " + name);
    return std::make_unique<AbiCommission>(it->second, self);
}

}
