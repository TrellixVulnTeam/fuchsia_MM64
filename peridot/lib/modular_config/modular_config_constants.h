// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_MODULAR_CONFIG_MODULAR_CONFIG_CONSTANTS_H_
#define PERIDOT_LIB_MODULAR_CONFIG_MODULAR_CONFIG_CONSTANTS_H_

namespace modular_config {

constexpr char kBasemgrConfigName[] = "basemgr";
constexpr char kSessionmgrConfigName[] = "sessionmgr";
constexpr char kSessionmgrUrl[] =
    "fuchsia-pkg://fuchsia.com/sessionmgr#meta/sessionmgr.cmx";
constexpr char kStartupConfigPath[] = "/config/data/startup.config";
constexpr char kStartupConfigDirPath[] = "/config/data";
constexpr char kOverridenStartupConfigPath[] =
    "/config_override/data/startup.config";
constexpr char kOverridenConfigDirPath[] = "/config_override/data";
constexpr char kStartupConfigFileName[] = "startup.config";
constexpr char kTrue[] = "true";

// Presentation constants
constexpr char kDisplayUsage[] = "display_usage";
constexpr char kHandheld[] = "handheld";
constexpr char kClose[] = "close";
constexpr char kNear[] = "near";
constexpr char kMidrange[] = "midrange";
constexpr char kFar[] = "far";
constexpr char kUnknown[] = "unknown";
constexpr char kScreenHeight[] = "screen_height";
constexpr char kScreenWidth[] = "screen_width";

// Cloud provider constants
constexpr char kCloudProvider[] = "cloud_provider";
constexpr char kLetLedgerDecide[] = "LET_LEDGER_DECIDE";
constexpr char kFromEnvironment[] = "FROM_ENVIRONMENT";
constexpr char kNone[] = "NONE";

// Basemgr constants
constexpr char kEnableCobalt[] = "enable_cobalt";
constexpr char kEnablePresenter[] = "enable_presenter";
constexpr char kTest[] = "test";
constexpr char kUseMinfs[] = "use_minfs";
constexpr char kUseSessionShellForStoryShellFactory[] =
    "use_session_shell_for_story_shell_factory";

// Sessionmgr constants
constexpr char kComponentArgs[] = "component_args";
constexpr char kUri[] = "uri";
constexpr char kArgs[] = "args";
constexpr char kEnableStoryShellPreload[] = "enable_story_shell_preload";
constexpr char kStartupAgents[] = "startup_agents";
constexpr char kSessionAgents[] = "session_agents";
constexpr char kUseMemfsForLedger[] = "use_memfs_for_ledger";

// Shell constants
constexpr char kDefaultBaseShellUrl[] =
    "fuchsia-pkg://fuchsia.com/dev_base_shell#meta/dev_base_shell.cmx";
constexpr char kDefaultSessionShellUrl[] =
    "fuchsia-pkg://fuchsia.com/ermine_session_shell#meta/"
    "ermine_session_shell.cmx";
constexpr char kDefaultStoryShellUrl[] =
    "fuchsia-pkg://fuchsia.com/mondrian#meta/mondrian.cmx";
constexpr char kBaseShell[] = "base_shell";
constexpr char kSessionShells[] = "session_shells";
constexpr char kStoryShellUrl[] = "story_shell_url";
constexpr char kUrl[] = "url";
constexpr char kKeepAliveAfterLogin[] = "keep_alive_after_login";

// Various config constants that will be deprecated in favor for the new names.
constexpr char kBaseShellArgs[] = "base_shell_args";
constexpr char kStoryShell[] = "story_shell";
constexpr char kStoryShellArgs[] = "story_shell_args";
constexpr char kSessionShell[] = "session_shell";
constexpr char kSessionShellArgs[] = "session_shell_args";
constexpr char kSessionmgrArgs[] = "sessionmgr_args";
constexpr char kDisableStatistics[] = "disable_statistics";
constexpr char kNoCloudProviderForLedger[] = "no_cloud_provider_for_ledger";
constexpr char kNoMinfs[] = "no_minfs";
constexpr char kRunBaseShellWithTestRunner[] =
    "run_base_shell_with_test_runner";
constexpr char kUseCloudProviderFromEnvironment[] =
    "use_cloud_provider_from_environment";

}  // namespace modular_config

#endif  // PERIDOT_LIB_MODULAR_CONFIG_MODULAR_CONFIG_CONSTANTS_H_