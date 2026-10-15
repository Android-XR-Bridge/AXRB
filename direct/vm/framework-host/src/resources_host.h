// Natives behind android.content.res.AssetManager: the process's resource
// tables (framework package 0x01 = cookie 0, application package 0x7f =
// cookie 1), compiled XML blocks and non-asset files (res/...). Internal to
// arun_framework_host.
#pragma once

#include <memory>

#include "arun/asset_set.h"
#include "arun/res_table.h"
#include "arun/status.h"

namespace arun::pkg {
class ZipReader;
}
namespace arun::vm {
class Runtime;
}

namespace arun::fw {

class ResourcesHost {
 public:
  // Loads resources.arsc from both packages. The application may have none;
  // the framework package must.
  static Result<std::unique_ptr<ResourcesHost>> Create(std::shared_ptr<pkg::ZipReader> framework,
                                                       std::shared_ptr<pkg::ZipReader> app);
  void RegisterNatives(vm::Runtime* rt);

  res::AssetSet& assets() { return assets_; }
  const pkg::ZipReader* apk(int cookie) const;

 private:
  ResourcesHost() = default;
  std::shared_ptr<pkg::ZipReader> apks_[2];
  std::unique_ptr<res::ResourceTable> tables_[2];
  res::AssetSet assets_;
};

}  // namespace arun::fw
