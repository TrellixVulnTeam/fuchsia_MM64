# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

if [[ -n "${ZSH_VERSION}" ]]; then
  source "$(cd "$(dirname "${(%):-%x}")/.." >/dev/null 2>&1 && pwd)"/tools/devshell/lib/vars.sh || return $?
else
  source "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." >/dev/null 2>&1 && pwd)"/tools/devshell/lib/vars.sh || return $?
fi

# __patched_path <old-regex> <new-component>
# Prints a new path value based on the current $PATH, removing path components
# that match <old-regex> and adding <new-component> to the end.
function __patched_path {
    local old_regex="$1"
    local new_component="$2"
    local stripped
    # Put each PATH component on a line, delete any lines that match the regex,
    # then glue back together with ':' characters.
    stripped="$(
        set -o pipefail &&
        echo "${PATH}" |
        tr ':' '\n' |
        grep -v -E "^${old_regex}$" |
        tr '\n' ':'
    )"
    # The trailing newline will have become a colon, so no need to add another
    # one here.
    echo "${stripped}${new_component}"
}

### fx-update-path: add useful tools to the PATH

# Add tools to path, removing prior tools directory if any. This also
# matches the Zircon tools directory added by zset, so add it back too.
function fx-update-path {
  local jiri_bin_dir="${FUCHSIA_DIR}/.jiri_root/bin"
  export PATH="$(__patched_path "${jiri_bin_dir}" "${jiri_bin_dir}")"

  export PATH="$(__patched_path "${FUCHSIA_DIR}/scripts/git" "${FUCHSIA_DIR}/scripts/git")"

  local rust_dir="$(source "${FUCHSIA_DIR}/buildtools/vars.sh" && echo -n "${BUILDTOOLS_RUST_DIR}/bin")"
  export PATH="$(__patched_path "${rust_dir}" "${rust_dir}")"

  # XXX(raggi): these can get stale, so this really needs rework. Probably the
  # only way to handle this more correctly is to wrap fx in a function that
  # re-runs fx-update-path whenever `fx set` or `fx use` succeed.
  local fuchsia_tools_dir="$(fx-config-read 2>/dev/null; echo "${FUCHSIA_BUILD_DIR}/tools")"
  local zircon_tools_dir="$(fx-config-read 2>/dev/null; echo "${ZIRCON_TOOLS_DIR}")"

  local tools_dirs="${zircon_tools_dir}:${fuchsia_tools_dir}"
  if [[ "${tools_dirs}" != ":" ]]; then
    export PATH="$(__patched_path \
        "${FUCHSIA_OUT_DIR}/[^/]*/tools" \
        "${tools_dirs}"
    )"
  fi
}

### fx-prompt-info: prints the current configuration for display in a prompt

function fx-prompt-info {
  # Run fx-config-read in a subshell to avoid polluting this shell's environment
  # with data from the config file, which can change without updating this
  # shell's environment.
  (
    fx-config-read
    echo "${FUCHSIA_BUILD_DIR##*/}"
  )
}

### fx-set-prompt: displays the current configuration in the prompt

function fx-set-prompt {
  if [[ -n "${ZSH_VERSION}" ]]; then
    autoload -Uz colors && colors
    setopt PROMPT_SUBST
    export PS1='%B%F{yellow}[$(fx-prompt-info)] %B%F{blue}%m:%~%#%f%b '
    export PS2='%B%F{blue}>%f%b '
  else
    export PS1='\[\e[0;1;33m\][$(fx-prompt-info)] \[\e[34m\]\h:\w\\$\[\e[0m\] '
    export PS2='\[\e[0;1;34m\]>\[\e[0m\] '
  fi
}

### fd: navigate to directories with autocomplete

# $ fd --help   # for usage.
function fd {
  local fd_python
  local dest
  fd_python="${FUCHSIA_DIR}/scripts/fd.py"
  dest=$(eval ${fd_python} "$@")
  cd -- "${dest}"
}

if [[ -z "${ZSH_VERSION}" ]]; then
  function __fd {
    local cur
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    if [[ ${cur:0:2} == "//" ]]; then
      COMPREPLY=($(/bin/ls -dp1 ${FUCHSIA_DIR}/${cur}* 2>/dev/null | \
        sed -n "s|^${FUCHSIA_DIR}/\(.*/\)\$|\1|p" | xargs echo))
    else
      COMPREPLY=($(/bin/ls -dp1 ${cur}* 2>/dev/null | grep "/$" | xargs echo))
    fi
  }
  complete -o nospace -F __fd fd
fi

### fx-go: alias of fd, for backward compatibility

function fx-go {
  echo "fx-go is to be deprecated in Q1 2018 in favor of 'fd'. For more help, fd --help"
  fd "$@"
}


# Support command-line auto-completions for the fx command.
if [[ -z "${ZSH_VERSION}" ]]; then
  function __fx_complete_cmd {
    local cmd cur prev
    cmd="${COMP_WORDS[1]}"
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"

    case "${cmd}" in
      set)
        if [[ ${COMP_CWORD} -eq 2 ]]; then
          COMPREPLY=($(compgen -W "x64 arm64" "${cur}"))
          return
        fi
        ;;

      set-petal)
        if [[ ${COMP_CWORD} -eq 2 ]]; then
          COMPREPLY=($(compgen -W "garnet peridot topaz" "${cur}"))
          return
        fi
        ;;
    esac
  }

  function __fx {
    COMPREPLY=()
    if [[ ${COMP_CWORD} -eq 1 ]]; then
      COMPREPLY=($(/bin/ls -dp1 ${FUCHSIA_DIR}/tools/devshell/${COMP_WORDS[1]}* 2>/dev/null | \
        sed -n "s|^${FUCHSIA_DIR}/tools/devshell/\([^/]*\)\$|\1|p" | xargs echo))
    else
      __fx_complete_cmd
    fi
  }
  complete -o default -F __fx fx
fi
