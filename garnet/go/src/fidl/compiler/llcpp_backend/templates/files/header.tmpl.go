// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package files

const Header = `
{{- define "Header" -}}
// WARNING: This file is machine generated by fidlgen.

#pragma once

#include <lib/fidl/internal.h>
#include <lib/fidl/cpp/vector_view.h>
#include <lib/fidl/cpp/string_view.h>
#include <lib/fidl/llcpp/array.h>
#include <lib/fidl/llcpp/coding.h>
#include <lib/fidl/llcpp/traits.h>
#include <lib/fidl/llcpp/transaction.h>
{{ range .HandleTypes -}}
#include <lib/zx/{{ . }}.h>
{{ end -}}
#include <zircon/fidl.h>
{{ if .LLHeaders -}}
{{ "" }}
{{ range .LLHeaders -}}
#include <{{ . }}>
{{ end -}}
{{ end -}}

{{- range .Library }}
namespace {{ . }} {
{{- end }}
{{ "" }}

{{- range .Decls }}
{{- if Eq .Kind Kinds.Bits }}{{ template "BitsForwardDeclaration" . }}{{- end }}
{{- if Eq .Kind Kinds.Enum }}{{ template "EnumForwardDeclaration" . }}{{- end }}
{{- if Eq .Kind Kinds.Interface }}{{ template "InterfaceForwardDeclaration" . }}{{- end }}
{{- if Eq .Kind Kinds.Struct }}{{ template "StructForwardDeclaration" . }}{{- end }}
{{- if Eq .Kind Kinds.Table }}{{ template "TableForwardDeclaration" . }}{{- end }}
{{- if Eq .Kind Kinds.Union }}{{ template "UnionForwardDeclaration" . }}{{- end }}
{{- if Eq .Kind Kinds.XUnion }}{{ template "XUnionForwardDeclaration" . }}{{- end }}
{{- end }}

{{- range .Decls }}
{{- if Eq .Kind Kinds.Const }}{{ template "ConstDeclaration" . }}{{- end }}
{{- if Eq .Kind Kinds.Interface }}{{ template "InterfaceDeclaration" . }}{{- end }}
{{- if Eq .Kind Kinds.Struct }}{{ template "StructDeclaration" . }}{{- end }}
{{- if Eq .Kind Kinds.Union }}{{ template "UnionDeclaration" . }}{{- end }}
{{- if Eq .Kind Kinds.XUnion }}{{ template "XUnionDeclaration" . }}{{- end }}
{{- end }}
{{ "" }}

{{- range .LibraryReversed }}
}  // namespace {{ . }}
{{- end }}

namespace fidl {

{{- range .Decls }}
{{- if Eq .Kind Kinds.Interface }}{{ template "InterfaceTraits" . }}{{- end }}
{{- if Eq .Kind Kinds.Struct }}{{ template "StructTraits" . }}{{- end }}
{{- if Eq .Kind Kinds.Union }}{{ template "UnionTraits" . }}{{- end }}
{{- if Eq .Kind Kinds.XUnion }}{{ template "XUnionTraits" . }}{{- end }}
{{- end }}

}  // namespace fidl
{{ end }}
`
