// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/console.h"

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#ifndef __Fuchsia__
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#endif

#include <filesystem>

#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/system.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_parser.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/split_string.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/fxl/strings/trim.h"

namespace zxdb {

namespace {

const char* kHistoryFilename = ".zxdb_history";

#ifndef __Fuchsia__

termios stdout_saved_termios;
struct sigaction saved_abort;
struct sigaction saved_segv;

void TerminalRestoreSignalHandler(int sig, siginfo_t* info, void* ucontext) {
  struct sigaction _ignore;

  if (sig == SIGABRT) {
    sigaction(SIGABRT, &saved_abort, &_ignore);
  } else if (sig == SIGSEGV) {
    sigaction(SIGSEGV, &saved_segv, &_ignore);
  } else {
    // Weird, but I'm not about to assert inside a signal handler.
    return;
  }

  tcsetattr(STDOUT_FILENO, TCSAFLUSH, &stdout_saved_termios);
  raise(sig);
}

void PreserveStdoutTermios() {
  if (!isatty(STDOUT_FILENO))
    return;

  if (tcgetattr(STDOUT_FILENO, &stdout_saved_termios) < 0)
    return;

  struct sigaction restore_handler;

  restore_handler.sa_sigaction = TerminalRestoreSignalHandler;
  restore_handler.sa_flags = SA_SIGINFO;

  sigaction(SIGABRT, &restore_handler, &saved_abort);
  sigaction(SIGSEGV, &restore_handler, &saved_segv);
}

#else

void PreserveStdoutTermios() {}

#endif  // !__Fuchsia__

}  // namespace

Console* Console::singleton_ = nullptr;

Console::Console(Session* session)
    : context_(session), line_input_("[zxdb] "), weak_factory_(this) {
  FXL_DCHECK(!singleton_);
  singleton_ = this;

  // Set the line input completion callback that can know about our context.
  // OK to bind |this| since we own the line_input object.
  auto fill_command_context = [this](Command* cmd) {
    context_.FillOutCommand(cmd);  // Ignore errors, this is for autocomplete.
  };
  line_input_.set_completion_callback(
      [fill_command_context](
          const std::string& prefix) -> std::vector<std::string> {
        return GetCommandCompletions(prefix, fill_command_context);
      });

  // Set stdin to async mode or OnStdinReadable will block.
  fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL, 0) | O_NONBLOCK);
}

Console::~Console() {
  FXL_DCHECK(singleton_ == this);
  singleton_ = nullptr;

  if (!SaveHistoryFile())
    Output(Err("Could not save history file to $HOME/%s.\n", kHistoryFilename));
}

void Console::Init() {
  PreserveStdoutTermios();
  line_input_.BeginReadLine();

  stdio_watch_ = debug_ipc::MessageLoop::Current()->WatchFD(
      debug_ipc::MessageLoop::WatchMode::kRead, STDIN_FILENO, this);

  LoadHistoryFile();
}

void Console::LoadHistoryFile() {
  std::filesystem::path path(getenv("HOME"));
  if (path.empty())
    return;
  path /= kHistoryFilename;

  std::string data;
  if (!files::ReadFileToString(path, &data))
    return;

  auto history = fxl::SplitStringCopy(data, "\n", fxl::kTrimWhitespace,
                                      fxl::kSplitWantNonEmpty);

  for (const std::string& cmd : history)
    line_input_.AddToHistory(cmd);
}

bool Console::SaveHistoryFile() {
  char* home = getenv("HOME");
  if (!home)
    return false;

  // We need to invert the order the deque has the entries.
  std::string history_data;
  const auto& history = line_input_.history();
  for (auto it = history.rbegin(); it != history.rend(); it++) {
    auto trimmed = fxl::TrimString(*it, " ");
    // We ignore empty entries or quit commands.
    if (trimmed.empty() || trimmed == "quit" || trimmed == "q" ||
        trimmed == "exit") {
      continue;
    }

    history_data.append(trimmed.ToString()).append("\n");
  }

  auto filepath = std::filesystem::path(home) / kHistoryFilename;
  return files::WriteFile(filepath, history_data.data(), history_data.size());
}

void Console::Output(const OutputBuffer& output) {
  // Since most operations are asynchronous, we have to hide the input line
  // before printing anything or it will get appended to whatever the user is
  // typing on the screen.
  //
  // TODO(brettw) This can cause flickering. A more advanced system would
  // do more fancy console stuff to output above the input line so we'd
  // never have to hide it.

  // Make sure stdout is in blocking mode since normal output won't expect
  // non-blocking mode. We can get in this state if stdin and stdout are the
  // same underlying handle because the constructor sets stdin to O_NONBLOCK
  // so we can asynchronously wait for input.
  int old_bits = fcntl(STDIN_FILENO, F_GETFL, 0);
  if (old_bits & O_NONBLOCK)
    fcntl(STDOUT_FILENO, F_SETFL, old_bits & ~O_NONBLOCK);

  line_input_.Hide();
  output.WriteToStdout();
  line_input_.Show();

  if (old_bits & O_NONBLOCK)
    fcntl(STDOUT_FILENO, F_SETFL, old_bits);
}

void Console::Output(const std::string& s) {
  OutputBuffer buffer;
  buffer.Append(s);
  Output(buffer);
}

void Console::Output(const Err& err) {
  OutputBuffer buffer;
  buffer.Append(err);
  Output(buffer);
}

void Console::Clear() {
  // We write directly instead of using Output because WriteToStdout expects
  // to append '\n' to outputs and won't flush it explicitly otherwise.
  line_input_.Hide();
  const char ff[] = "\033c";  // Form feed.
  write(STDOUT_FILENO, ff, sizeof(ff));
  line_input_.Show();
}

Console::Result Console::DispatchInputLine(const std::string& line,
                                           CommandCallback callback) {
  Command cmd;
  Err err;
  if (line.empty()) {
    // Repeat the previous command, don't add to history.
    err = ParseCommand(previous_line_, &cmd);
  } else {
    line_input_.AddToHistory(line);
    err = ParseCommand(line, &cmd);
    previous_line_ = line;
  }

  if (err.ok()) {
    if (cmd.verb() == Verb::kQuit) {
      return Result::kQuit;
    } else {
      err = context_.FillOutCommand(&cmd);
      if (!err.has_error()) {
        err = DispatchCommand(&context_, cmd, callback);

        if (cmd.thread() && cmd.verb() != Verb::kNone) {
          // Show the right source/disassembly for the next listing.
          context_.SetSourceAffinityForThread(
              cmd.thread(), GetVerbRecord(cmd.verb())->source_affinity);
        }
      }
    }
  }

  if (err.has_error()) {
    OutputBuffer out;
    out.Append(err);
    Output(out);
  }
  return Result::kContinue;
}

Console::Result Console::ProcessInputLine(const std::string& line,
                                          CommandCallback callback) {
  Result result = DispatchInputLine(line, callback);
  if (result == Result::kQuit)
    debug_ipc::MessageLoop::Current()->QuitNow();
  return result;
}

void Console::OnFDReady(int fd, bool readable, bool, bool) {
  if (!readable) {
    return;
  }

  char ch;
  while (read(STDIN_FILENO, &ch, 1) > 0) {
    if (line_input_.OnInput(ch)) {
      // EOF (ctrl-d) should exit gracefully.
      if (line_input_.eof()) {
        line_input_.EnsureNoRawMode();
        Output("\n");
        debug_ipc::MessageLoop::Current()->QuitNow();
        return;
      }

      std::string line = line_input_.line();
      Result result = ProcessInputLine(line);
      if (result == Result::kQuit)
        return;
      line_input_.BeginReadLine();
    }
  }
}

fxl::WeakPtr<Console> Console::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

}  // namespace zxdb
