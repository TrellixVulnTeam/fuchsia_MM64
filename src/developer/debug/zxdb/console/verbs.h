// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>
#include <string>
#include <vector>

#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/console/command_group.h"
#include "src/developer/debug/zxdb/console/switch_record.h"

namespace zxdb {

class Command;
class ConsoleContext;

// Indicates whether a command implies either source or assembly context. This
// can be used by the frontend as a hint for what to show for the next stop.
enum class SourceAffinity {
  // The command applies to source code (e.g. "next").
  kSource,

  // The command applies to assembly code (e.g. "stepi", "disassemble").
  kAssembly,

  // This command does not imply any source or disassembly relation.
  kNone
};

enum class Verb {
  kNone = 0,

  kAspace,
  kAttach,
  kAuth,
  kBacktrace,
  kBreak,
  kClear,
  kCls,
  kConnect,
  kContinue,
  kDetach,
  kDisassemble,
  kDisconnect,
  kDown,
  kEdit,
  kFinish,
  kGet,
  kHardwareBreakpoint,
  kHelp,
  kJump,
  kKill,
  kLibs,
  kList,
  kListProcesses,
  kLocals,
  kMemAnalyze,
  kMemRead,
  kNew,
  kNext,
  kNexti,
  kOpenDump,
  kPause,
  kPrint,
  kQuit,
  kQuitAgent,
  kRegs,
  kRun,
  kSet,
  kStack,
  kStderr,
  kStdout,
  kStep,
  kStepi,
  kSymInfo,
  kSymNear,
  kSymSearch,
  kSymStat,
  kSysInfo,
  kUntil,
  kUp,

  // Adding a new one? Add in one of the functions GetVerbs() calls.
  kLast  // Not a real verb, keep last.
};

struct VerbRecord {
  // Type for the callback that runs a command.
  using CommandExecutor = std::function<Err(ConsoleContext*, const Command&)>;

  // Executor that is able to receive a callback that it can then pass on.
  using CommandExecutorWithCallback = std::function<Err(
      ConsoleContext*, const Command&, std::function<void(Err)>)>;

  // Type for the callback to complete the command's arguments. The command
  // will be filled out as far as is possible for the current parse, and the
  // completions should be filled with suggestions for the next token, each of
  // which should begin with the given prefix.
  using CommandCompleter =
      std::function<void(const Command& command, const std::string& prefix,
                         std::vector<std::string>* completions)>;

  VerbRecord();

  // The help will be referenced by pointer. It is expected to be a static
  // string.
  VerbRecord(CommandExecutor exec, std::initializer_list<std::string> aliases,
             const char* short_help, const char* help, CommandGroup group,
             SourceAffinity source_affinity = SourceAffinity::kNone);
  VerbRecord(CommandExecutorWithCallback exec_cb,
             std::initializer_list<std::string> aliases, const char* short_help,
             const char* help, CommandGroup group,
             SourceAffinity source_affinity = SourceAffinity::kNone);
  VerbRecord(CommandExecutor exec, CommandCompleter complete,
             std::initializer_list<std::string> aliases, const char* short_help,
             const char* help, CommandGroup group,
             SourceAffinity source_affinity = SourceAffinity::kNone);
  VerbRecord(CommandExecutorWithCallback exec_cb, CommandCompleter complete,
             std::initializer_list<std::string> aliases, const char* short_help,
             const char* help, CommandGroup group,
             SourceAffinity source_affinity = SourceAffinity::kNone);
  ~VerbRecord();

  CommandExecutor exec = nullptr;
  CommandExecutorWithCallback exec_cb = nullptr;

  // These are the user-typed strings that will name this verb. The [0]th one
  // is the canonical name.
  std::vector<std::string> aliases;

  const char* short_help = nullptr;  // One-line help.
  const char* help = nullptr;
  std::vector<SwitchRecord> switches;  // Switches supported by this verb.

  CommandGroup command_group = CommandGroup::kGeneral;
  SourceAffinity source_affinity = SourceAffinity::kNone;

  CommandCompleter complete = nullptr;
};

// Returns all known verbs. The contents of this map will never change once
// it is called.
const std::map<Verb, VerbRecord>& GetVerbs();

// Converts the given verb to the canonical name.
std::string VerbToString(Verb v);

// Returns the record for the given verb. If the verb is not registered (should
// not happen) or is kNone (this is what noun-only commands use), returns null.
const VerbRecord* GetVerbRecord(Verb verb);

// Returns the mapping from possible inputs to the noun/verb. This is an
// inverted version of the map returned by GetNouns()/GetVerbs();
const std::map<std::string, Verb>& GetStringVerbMap();

// These functions add records for the verbs they support to the given map.
void AppendBreakpointVerbs(std::map<Verb, VerbRecord>* verbs);
void AppendControlVerbs(std::map<Verb, VerbRecord>* verbs);
void AppendMemoryVerbs(std::map<Verb, VerbRecord>* verbs);
void AppendProcessVerbs(std::map<Verb, VerbRecord>* verbs);
void AppendSettingsVerbs(std::map<Verb, VerbRecord>* verbs);
void AppendSharedVerbs(std::map<Verb, VerbRecord>* verbs);
void AppendSymbolVerbs(std::map<Verb, VerbRecord>* verbs);
void AppendSystemVerbs(std::map<Verb, VerbRecord>* verbs);
void AppendThreadVerbs(std::map<Verb, VerbRecord>* verbs);

}  // namespace zxdb
