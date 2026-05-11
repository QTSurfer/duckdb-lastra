#pragma once

#include "duckdb.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

#define LASTRA_EXTENSION_VERSION "0.2.0"

namespace duckdb {

class LastraExtension : public Extension {
public:
    void Load(ExtensionLoader &loader) override;
    std::string Name() override;
    std::string Version() const override;
};

} // namespace duckdb
