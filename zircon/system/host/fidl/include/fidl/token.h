// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_TOKEN_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_TOKEN_H_

#include <stdint.h>

#include "source_location.h"
#include "string_view.h"

namespace fidl {

// A Token represents a typed view into a source buffer. That is, it
// has a TokenKind, and it has a buffer representing the data
// corresponding to the token. No processing is done on the data:
// string or numeric literals aren't further parsed, identifiers
// uniqued, and so on.
class Token {
public:
    enum Kind : uint8_t {
#define TOKEN(Name) k##Name,
#include "fidl/token_definitions.inc"
#undef TOKEN
    };

    enum Subkind : uint8_t {
        kNone = 0,
#define KEYWORD(Name, Spelling) k##Name,
#include "fidl/token_definitions.inc"
#undef KEYWORD
    };

    class KindAndSubkind {
    public:
        constexpr KindAndSubkind(Kind kind, Subkind subkind)
            : kind_(kind), subkind_(subkind) {}

        constexpr Kind kind() const { return kind_; }
        constexpr Subkind subkind() const { return subkind_; }
        constexpr uint16_t combined() const { return uint16_t(kind_) | uint16_t(subkind_ << 8); }
    private:
        Kind kind_;
        Subkind subkind_;
    };

    Token(SourceLocation previous_end, SourceLocation location, Kind kind, Subkind subkind)
        : previous_end_(previous_end), location_(location), kind_and_subkind_(KindAndSubkind(kind, subkind)) {}

    Token()
        : Token(SourceLocation(), SourceLocation(), Token::Kind::kNotAToken, Token::Subkind::kNone) {}

    static const char* Name(KindAndSubkind kind_and_subkind) {
        switch (kind_and_subkind.combined()) {
#define TOKEN(Name)            \
    case Token::Kind::k##Name: \
        return #Name;
#include "fidl/token_definitions.inc"
#undef TOKEN
#define KEYWORD(Name, Spelling)                                                               \
    case Token::KindAndSubkind(Token::Kind::kIdentifier, Token::Subkind::k##Name).combined(): \
        return #Spelling;
#include "fidl/token_definitions.inc"
#undef KEYWORD
        default:
            return "<unknown token>";
        }
    }

    StringView data() const { return location_.data(); }
    const SourceLocation& location() const { return location_; }
    void set_previous_end(SourceLocation location) { previous_end_ = location; }
    SourceLocation previous_end() const { return previous_end_; }
    Kind kind() const { return kind_and_subkind_.kind(); }
    Subkind subkind() const { return kind_and_subkind_.subkind(); }
    KindAndSubkind kind_and_subkind() const { return kind_and_subkind_; }

private:
    // The end of the previous token.  Everything between this and location_ is
    // somehow uninteresting to the parser (whitespace, comments, discarded
    // braces, etc).
    SourceLocation previous_end_;
    SourceLocation location_;
    KindAndSubkind kind_and_subkind_;
};

} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_TOKEN_H_
