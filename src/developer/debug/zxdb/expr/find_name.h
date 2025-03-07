// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

#include "src/developer/debug/zxdb/symbols/visit_scopes.h"

namespace zxdb {

class CodeBlock;
class Collection;
class DataMember;
class Identifier;
class IndexWalker;
class FoundMember;
class FoundName;
class Location;
class ModuleSymbols;
class ProcessSymbols;
class SymbolContext;
class TargetSymbols;
class Variable;

// FindName can search for different levels of things depending on how much
// context it's given. This class encapsulates all of these variants.
struct FindNameContext {
  // No symbol context. This can be useful when searching for names on
  // structures where there is no environmental state needed.
  FindNameContext() = default;

  // Search everything given a live context. The current module is extracted
  // from the given symbol context if possible.
  //
  // Note that this tolerates a null ProcessSymbols which sets no symbol
  // paths. This is useful for some tests.
  FindNameContext(const ProcessSymbols* ps, const SymbolContext& symbol_context,
                  const CodeBlock* cb = nullptr);

  // Searches a target's symbols. This is used to search for symbols in a
  // non-running program.
  explicit FindNameContext(const TargetSymbols* ts);

  // Together TargetSymbols and ModuleSymbols control what is searched. They
  // are both optional, producing this behavior:
  //
  // - Both TargetSymbols and ModuleSymbols: All modules will be searched with
  //   the given one searched first. This is to give preference to the current
  //   module in the case of multiple matches.
  //
  // - TargetSymbols but not ModuleSymbols: All modules will be searched in an
  //   arbitrary order.
  //
  // - ModuleSymbols but not TargetSymbols: Only the given module will be
  //   searched for symbols.
  //
  // - Neither TargetSymbols nor ModuleSymbols: No symbol lookups are done.
  const TargetSymbols* target_symbols = nullptr;
  const ModuleSymbols* module_symbols = nullptr;

  // If given, local variables, local types, and |this| will be searched.
  // Otherwise, only global symbols will be searched.
  const CodeBlock* block = nullptr;
};

// By default this will find the first exact match of any kind.
struct FindNameOptions {
  // How to match the name.
  //
  // Note that prefix matching doesn't currently work for templates. Prefix
  // matching is currently used for autocomplete where the full type name is
  // desired, not just the base template name. And supporting this requires
  // uniquifying names (since many template types could be the same underlying
  // template) that's annoying to implement.
  enum HowMatch { kPrefix, kExact };
  HowMatch how = kExact;

  // This constructor's argument indicates whether the caller wants to default
  // to finding all or no types (presumably in the "no types" case, the caller
  // will set one or more to true afterward).
  enum InitialKinds : bool { kNoKinds = false, kAllKinds = true };
  explicit FindNameOptions(InitialKinds initial)
      : find_types(initial),
        find_functions(initial),
        find_vars(initial),
        find_templates(initial),
        find_namespaces(initial) {}

  // The types of named things that will be matched.
  bool find_types = true;
  bool find_functions = true;  // Global and member functions.
  bool find_vars = true;       // Local and "this" member vars.
  bool find_templates = true;  // Templatized types without <...>.
  bool find_namespaces = true;

  size_t max_results = 1;
};

// Main variable and type name finding function. Searches the local, "this",
// and global scopes for one or more things with a matching name. The first
// version finds the first exact match of any type, the second uses the
// options to customize what and how many results are returned.
FoundName FindName(const FindNameContext& context,
                   const Identifier& identifier);
void FindName(const FindNameContext& context, const FindNameOptions& options,
              const Identifier& looking_for, std::vector<FoundName>* results);

// Type-specific finding -------------------------------------------------------

// Searches the code block for local variables. This includes all nested code
// blocks and function parameters, but does not go into the "this" class or any
// non-function scopes like the current or global namespace (that's what the
// later functions do).
//
// The "visit" variant calls the callback for every variable in order of
// priority for as long as the visitor reports "continue." The "find" variant
// does an exact string search, the "prefix" variant does a prefix search.
VisitResult VisitLocalVariables(
    const CodeBlock* block,
    const std::function<VisitResult(const Variable*)>& visitor);
void FindLocalVariable(const FindNameOptions& options, const CodeBlock* block,
                       const Identifier& looking_for,
                       std::vector<FoundName>* results);

// Searches for the named variable or type on the given collection. This is the
// lower-level function and assumes a valid object. The result can be either a
// kType or a kMemberVariable.
//
// If the ProcessSymbols is non-null, this function will also search for
// type names defined in the collection. Otherwise, only data members will be
// searched.
//
// The optional symbol context is the symbol context for the current code. it
// will be used to prioritize symbol searching to the current module if given.
//
// If the result is a member variable, the optional_object_ptr will be used to
// construct the FoundName object. It can be null if the caller does not have
// a variable for the object it's looking up (just doing a type query).
void FindMember(const FindNameContext& context, const FindNameOptions& options,
                const Collection* object, const Identifier& looking_for,
                const Variable* optional_object_ptr,
                std::vector<FoundName>* result);

// Attempts the resolve the given named member variable or type on the "this"
// pointer associated with the given code block. Fails if the function has no
// "this" pointer or the type name / data member isn't found.
//
// If the ProcessSymbols is non-null, this function will also search for
// type names defined in the collection. Otherwise, only data members will be
// searched.
//
// The optional symbol context is the symbol context for the current code. it
// will be used to prioritize symbol searching to the current module if given.
void FindMemberOnThis(const FindNameContext& context,
                      const FindNameOptions& options,
                      const Identifier& looking_for,
                      std::vector<FoundName>* result);

// Attempts to resolve the named |looking_for| in the index.
//
// The |current_scope| is the namespace to start looking in. If
// |search_containing| is true, parent scopes of the |current_scope| are also
// searched, otherwise only exact matches in that scope will be found.
VisitResult FindIndexedName(const FindNameContext& context,
                            const FindNameOptions& options,
                            const Identifier& current_scope,
                            const Identifier& looking_for,
                            bool search_containing,
                            std::vector<FoundName>* results);

// Searches a specific index and current namespace for a global variable or
// type of the given name. The current_scope would be the current namespace +
// class from where to start the search.
VisitResult FindIndexedNameInModule(const FindNameOptions& options,
                                    const ModuleSymbols* module_symbols,
                                    const Identifier& current_scope,
                                    const Identifier& looking_for,
                                    bool search_containing,
                                    std::vector<FoundName>* results);

}  // namespace zxdb
