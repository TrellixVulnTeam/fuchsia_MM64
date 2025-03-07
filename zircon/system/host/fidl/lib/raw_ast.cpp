// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains the implementations of the Accept methods for the AST
// nodes.  Generally, all they do is invoke the appropriate TreeVisitor method
// for each field of the node.

#include "fidl/raw_ast.h"
#include "fidl/tree_visitor.h"

#include <map>

namespace fidl {
namespace raw {

SourceElementMark::SourceElementMark(TreeVisitor* tv,
                                     const SourceElement& element)
    : tv_(tv), element_(element) {
    tv_->OnSourceElementStart(element_);
}

SourceElementMark::~SourceElementMark() {
    tv_->OnSourceElementEnd(element_);
}

void Identifier::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
}

void CompoundIdentifier::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    for (auto& i : components) {
        visitor->OnIdentifier(i);
    }
}

void StringLiteral::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
}

void NumericLiteral::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
}

void TrueLiteral::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
}

void FalseLiteral::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
}

void IdentifierConstant::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    visitor->OnCompoundIdentifier(identifier);
}

void LiteralConstant::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    visitor->OnLiteral(literal);
}

void Ordinal::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
}

void Attribute::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
}

void AttributeList::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    for (auto& i : attributes) {
        visitor->OnAttribute(i);
    }
}

void TypeConstructor::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    visitor->OnCompoundIdentifier(identifier);
    if (maybe_arg_type_ctor != nullptr)
        visitor->OnTypeConstructor(maybe_arg_type_ctor);
    if (maybe_handle_subtype != nullptr)
        visitor->OnHandleSubtype(*maybe_handle_subtype);
    if (maybe_size != nullptr)
        visitor->OnConstant(maybe_size);
    visitor->OnNullability(nullability);
}

void Using::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    visitor->OnCompoundIdentifier(using_path);
    if (maybe_alias != nullptr) {
        visitor->OnIdentifier(maybe_alias);
    }
    if (maybe_type_ctor != nullptr) {
        visitor->OnTypeConstructor(maybe_type_ctor);
    }
}

void BitsMember::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    if (attributes != nullptr) {
        visitor->OnAttributeList(attributes);
    }
    visitor->OnIdentifier(identifier);
    visitor->OnConstant(value);
}

void BitsDeclaration::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    if (attributes != nullptr) {
        visitor->OnAttributeList(attributes);
    }
    visitor->OnIdentifier(identifier);
    if (maybe_type_ctor != nullptr) {
        visitor->OnTypeConstructor(maybe_type_ctor);
    }
    for (auto member = members.begin(); member != members.end(); ++member) {
        visitor->OnBitsMember(*member);
    }
}

void ConstDeclaration::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    if (attributes != nullptr) {
        visitor->OnAttributeList(attributes);
    }
    visitor->OnTypeConstructor(type_ctor);
    visitor->OnIdentifier(identifier);
    visitor->OnConstant(constant);
}

void EnumMember::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    if (attributes != nullptr) {
        visitor->OnAttributeList(attributes);
    }
    visitor->OnIdentifier(identifier);
    visitor->OnConstant(value);
}

void EnumDeclaration::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    if (attributes != nullptr) {
        visitor->OnAttributeList(attributes);
    }
    visitor->OnIdentifier(identifier);
    if (maybe_type_ctor != nullptr) {
        visitor->OnTypeConstructor(maybe_type_ctor);
    }
    for (auto member = members.begin(); member != members.end(); ++member) {
        visitor->OnEnumMember(*member);
    }
}

void Parameter::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    visitor->OnTypeConstructor(type_ctor);
    visitor->OnIdentifier(identifier);
}

void ParameterList::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    for (auto parameter = parameter_list.begin(); parameter != parameter_list.end(); ++parameter) {
        visitor->OnParameter(*parameter);
    }
}

void InterfaceMethod::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    if (attributes != nullptr) {
        visitor->OnAttributeList(attributes);
    }
    if (ordinal != nullptr) {
        visitor->OnOrdinal(*ordinal);
    }
    visitor->OnIdentifier(identifier);
    if (maybe_request != nullptr) {
        visitor->OnParameterList(maybe_request);
    }
    if (maybe_response != nullptr) {
        visitor->OnParameterList(maybe_response);
    }
    if (maybe_error_ctor != nullptr) {
        visitor->OnTypeConstructor(maybe_error_ctor);
    }
}

void ComposeProtocol::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    visitor->OnCompoundIdentifier(protocol_name);
}

void InterfaceDeclaration::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    if (attributes != nullptr) {
        visitor->OnAttributeList(attributes);
    }
    visitor->OnIdentifier(identifier);
    for (auto superinterface = superinterfaces.begin();
        superinterface != superinterfaces.end();
        ++superinterface) {
        visitor->OnComposeProtocol(*superinterface);
    }
    for (auto method = methods.begin();
        method != methods.end();
        ++method) {
        visitor->OnInterfaceMethod(*method);
    }
}

void StructMember::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    if (attributes != nullptr) {
        visitor->OnAttributeList(attributes);
    }
    visitor->OnTypeConstructor(type_ctor);
    visitor->OnIdentifier(identifier);
    if (maybe_default_value != nullptr) {
        visitor->OnConstant(maybe_default_value);
    }
}

void StructDeclaration::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    if (attributes != nullptr) {
        visitor->OnAttributeList(attributes);
    }
    visitor->OnIdentifier(identifier);
    for (auto member = members.begin();
         member != members.end();
         ++member) {
        visitor->OnStructMember(*member);
    }
}

void TableMember::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    if (maybe_used != nullptr) {
        if (maybe_used->attributes != nullptr) {
            visitor->OnAttributeList(maybe_used->attributes);
        }
    }
    visitor->OnOrdinal(*ordinal);
    if (maybe_used != nullptr) {
        visitor->OnTypeConstructor(maybe_used->type_ctor);
        visitor->OnIdentifier(maybe_used->identifier);
        if (maybe_used->maybe_default_value != nullptr) {
            visitor->OnConstant(maybe_used->maybe_default_value);
        }
    }
}

void TableDeclaration::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    if (attributes != nullptr) {
        visitor->OnAttributeList(attributes);
    }
    visitor->OnIdentifier(identifier);
    for (auto member = members.begin();
         member != members.end();
         ++member) {
        visitor->OnTableMember(*member);
    }
}

void UnionMember::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    if (attributes != nullptr) {
        visitor->OnAttributeList(attributes);
    }
    visitor->OnTypeConstructor(type_ctor);
    visitor->OnIdentifier(identifier);
}

void UnionDeclaration::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    if (attributes != nullptr) {
        visitor->OnAttributeList(attributes);
    }
    visitor->OnIdentifier(identifier);
    for (auto member = members.begin();
         member != members.end();
         ++member) {
        visitor->OnUnionMember(*member);
    }
}

void XUnionMember::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    if (attributes != nullptr) {
        visitor->OnAttributeList(attributes);
    }

    visitor->OnTypeConstructor(type_ctor);
    visitor->OnIdentifier(identifier);
}

void XUnionDeclaration::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    if (attributes != nullptr) {
        visitor->OnAttributeList(attributes);
    }
    visitor->OnIdentifier(identifier);
    for (auto& member : members) {
        visitor->OnXUnionMember(member);
    }
}

void File::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    if (attributes != nullptr) {
        visitor->OnAttributeList(attributes);
    }
    visitor->OnCompoundIdentifier(library_name);
    for (auto& i : using_list) {
        visitor->OnUsing(i);
    }
    for (auto& i : const_declaration_list) {
        visitor->OnConstDeclaration(i);
    }
    for (auto& i : enum_declaration_list) {
        visitor->OnEnumDeclaration(i);
    }
    for (auto& i : interface_declaration_list) {
        visitor->OnInterfaceDeclaration(i);
    }
    for (auto& i : struct_declaration_list) {
        visitor->OnStructDeclaration(i);
    }
    for (auto& i : table_declaration_list) {
        visitor->OnTableDeclaration(i);
    }
    for (auto& i : union_declaration_list) {
        visitor->OnUnionDeclaration(i);
    }
    for (auto& i : xunion_declaration_list) {
        visitor->OnXUnionDeclaration(i);
    }
}

} // namespace raw
} // namespace fidl
