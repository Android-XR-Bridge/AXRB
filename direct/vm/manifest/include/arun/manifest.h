// AndroidManifest.xml as the runner needs it: identity, SDK levels, the
// application class, components and the launcher activity.
//
// Ownership: value types. Thread affinity: none. Error propagation: Result
// with kManifestMalformed / kManifestNoLauncher.
//
// Attributes in the android namespace are matched by framework resource id,
// exactly as the platform's package parser does, never by prefix text.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "arun/axml.h"
#include "arun/status.h"

namespace arun::manifest {

// A string attribute that may instead be a resource reference ("@string/x").
struct TextValue {
  std::optional<std::string> literal;
  std::uint32_t resource_id = 0;  // non-zero for a reference
};

struct IntentFilter {
  std::vector<std::string> actions;
  std::vector<std::string> categories;
};

struct Activity {
  std::string name;  // fully qualified class name
  std::optional<bool> exported;
  bool enabled = true;
  TextValue label;
  std::uint32_t theme = 0;
  std::string target_activity;  // for <activity-alias>, fully qualified
  bool is_alias = false;
  std::vector<IntentFilter> intent_filters;

  bool IsLauncher() const;
};

struct Manifest {
  std::string package;
  std::int64_t version_code = 0;
  std::string version_name;
  int min_sdk = 1;
  int target_sdk = 0;  // 0: not declared (defaults to min_sdk)
  std::string application_class;  // fully qualified; empty = android.app.Application
  TextValue application_label;
  std::uint32_t application_theme = 0;
  bool has_code = true;
  std::vector<Activity> activities;  // activities and aliases, in document order
  std::vector<std::string> uses_permissions;
  std::vector<std::string> uses_libraries;
  std::vector<std::string> uses_native_libraries;

  // The activity to start for android.intent.action.MAIN /
  // android.intent.category.LAUNCHER, with aliases resolved to their target.
  Result<std::string> LauncherActivity() const;
};

// "pkg" + ".Foo" -> "pkg.Foo"; "Foo" -> "pkg.Foo"; "a.b.Foo" unchanged.
std::string QualifyClassName(std::string_view package, std::string_view name);

Result<Manifest> Parse(std::span<const std::uint8_t> binary_xml);

}  // namespace arun::manifest
