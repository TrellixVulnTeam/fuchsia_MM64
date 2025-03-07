// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/dwarf_die_decoder.h"

#include "src/lib/fxl/logging.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFFormValue.h"

namespace zxdb {

namespace {

// The maximum nesting of abstract origin references we'll follow recursively
// before givin up. Prevents blowing out the stack for corrupt symbols.
constexpr int kMaxAbstractOriginRefsToFollow = 8;

// Decodes a cross-DIE reference. Return value will be !isValid() on failure.
llvm::DWARFDie DecodeReference(llvm::DWARFUnit* unit,
                               const llvm::DWARFFormValue& form) {
  switch (form.getForm()) {
    case llvm::dwarf::DW_FORM_ref1:
    case llvm::dwarf::DW_FORM_ref2:
    case llvm::dwarf::DW_FORM_ref4:
    case llvm::dwarf::DW_FORM_ref8:
    case llvm::dwarf::DW_FORM_ref_udata: {
      // A DWARF "form" is the way a value is encoded in the file. These
      // are all relative location of DIEs within the same unit.
      auto ref_value = form.getAsReferenceUVal();
      if (ref_value)
        return unit->getDIEForOffset(unit->getOffset() + *ref_value);
      break;
    }
    case llvm::dwarf::DW_FORM_ref_addr: {
      // This is an absolute DIE address which can be used across units.
      auto ref_value = form.getAsReferenceUVal();
      if (ref_value)
        return unit->getDIEForOffset(*ref_value);
      break;
    }
    default:
      // Note that we don't handle DW_FORM_ref_sig8, DW_FORM_ref_sup4, or
      // DW_FORM_ref_sup8. The "sig8" one requries a different type encoding
      // that our Clang toolchain doesn't seem to generate. The "sup4/8" ones
      // require a shared separate symbol file we don't use.
      break;
  }
  return llvm::DWARFDie();
}

}  // namespace

DwarfDieDecoder::DwarfDieDecoder(llvm::DWARFContext* context,
                                 llvm::DWARFUnit* unit)
    : context_(context),
      unit_(unit),
      extractor_(unit_->getDebugInfoExtractor()) {}

DwarfDieDecoder::~DwarfDieDecoder() = default;

void DwarfDieDecoder::AddPresenceCheck(llvm::dwarf::Attribute attribute,
                                       bool* present) {
  attrs_.emplace_back(
      attribute, [present](const llvm::DWARFFormValue&) { *present = true; });
}

void DwarfDieDecoder::AddBool(llvm::dwarf::Attribute attribute,
                              llvm::Optional<bool>* output) {
  attrs_.emplace_back(attribute, [output](const llvm::DWARFFormValue& form) {
    *output = !!form.getAsUnsignedConstant();
  });
}

void DwarfDieDecoder::AddUnsignedConstant(llvm::dwarf::Attribute attribute,
                                          llvm::Optional<uint64_t>* output) {
  attrs_.emplace_back(attribute, [output](const llvm::DWARFFormValue& form) {
    *output = form.getAsUnsignedConstant();
  });
}

void DwarfDieDecoder::AddSignedConstant(llvm::dwarf::Attribute attribute,
                                        llvm::Optional<int64_t>* output) {
  attrs_.emplace_back(attribute, [output](const llvm::DWARFFormValue& form) {
    *output = form.getAsSignedConstant();
  });
}

void DwarfDieDecoder::AddAddress(llvm::dwarf::Attribute attribute,
                                 llvm::Optional<uint64_t>* output) {
  attrs_.emplace_back(attribute, [output](const llvm::DWARFFormValue& form) {
    *output = form.getAsAddress();
  });
}

void DwarfDieDecoder::AddHighPC(llvm::Optional<HighPC>* output) {
  attrs_.emplace_back(
      llvm::dwarf::DW_AT_high_pc, [output](const llvm::DWARFFormValue& form) {
        if (form.isFormClass(llvm::DWARFFormValue::FC_Constant)) {
          auto as_constant = form.getAsUnsignedConstant();
          if (as_constant)
            *output = HighPC(true, *as_constant);
        } else if (form.isFormClass(llvm::DWARFFormValue::FC_Address)) {
          auto as_addr = form.getAsAddress();
          if (as_addr)
            *output = HighPC(false, *as_addr);
        }
      });
}

void DwarfDieDecoder::AddCString(llvm::dwarf::Attribute attribute,
                                 llvm::Optional<const char*>* output) {
  attrs_.emplace_back(attribute, [output](const llvm::DWARFFormValue& form) {
    *output = form.getAsCString();
  });
}

void DwarfDieDecoder::AddLineTableFile(llvm::dwarf::Attribute attribute,
                                       llvm::Optional<std::string>* output) {
  const llvm::DWARFDebugLine::LineTable* line_table =
      context_->getLineTableForUnit(unit_);
  const char* compilation_dir = unit_->getCompilationDir();
  if (line_table) {
    attrs_.emplace_back(attribute, [output, compilation_dir, line_table](
                                       const llvm::DWARFFormValue& form) {
      output->emplace();
      line_table->getFileNameByIndex(
          form.getAsUnsignedConstant().getValue(), compilation_dir,
          llvm::DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath,
          output->getValue());
    });
  }
}

void DwarfDieDecoder::AddReference(llvm::dwarf::Attribute attribute,
                                   llvm::Optional<uint64_t>* unit_offset,
                                   llvm::Optional<uint64_t>* global_offset) {
  attrs_.emplace_back(attribute, [unit_offset, global_offset](
                                     const llvm::DWARFFormValue& form) {
    // See DecodeReference() function above for more info.
    switch (form.getForm()) {
      case llvm::dwarf::DW_FORM_ref1:
      case llvm::dwarf::DW_FORM_ref2:
      case llvm::dwarf::DW_FORM_ref4:
      case llvm::dwarf::DW_FORM_ref8:
      case llvm::dwarf::DW_FORM_ref_udata:
        *unit_offset = form.getAsReferenceUVal();
        break;
      case llvm::dwarf::DW_FORM_ref_addr:
        *global_offset = form.getAsReferenceUVal();
        break;
      default:
        break;
    }
  });
}

void DwarfDieDecoder::AddReference(llvm::dwarf::Attribute attribute,
                                   llvm::DWARFDie* output) {
  attrs_.emplace_back(attribute,
                      [this, output](const llvm::DWARFFormValue& form) {
                        *output = DecodeReference(unit_, form);
                      });
}

void DwarfDieDecoder::AddFile(llvm::dwarf::Attribute attribute,
                              llvm::Optional<std::string>* output) {
  attrs_.emplace_back(
      attribute, [this, output](const llvm::DWARFFormValue& form) {
        llvm::Optional<uint64_t> file_index = form.getAsUnsignedConstant();
        if (!file_index)
          return;

        const llvm::DWARFDebugLine::LineTable* line_table =
            context_->getLineTableForUnit(unit_);
        const char* compilation_dir = unit_->getCompilationDir();

        std::string file_name;
        if (line_table->getFileNameByIndex(
                *file_index, compilation_dir,
                llvm::DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath,
                file_name))
          *output = std::move(file_name);
      });
}

void DwarfDieDecoder::AddAbstractParent(llvm::DWARFDie* output) {
  FXL_DCHECK(!abstract_parent_);
  abstract_parent_ = output;
}

void DwarfDieDecoder::AddCustom(
    llvm::dwarf::Attribute attribute,
    std::function<void(const llvm::DWARFFormValue&)> callback) {
  attrs_.emplace_back(attribute, std::move(callback));
}

bool DwarfDieDecoder::Decode(const llvm::DWARFDie& die) {
  return Decode(*die.getDebugInfoEntry());
}

bool DwarfDieDecoder::Decode(const llvm::DWARFDebugInfoEntry& die) {
  std::vector<llvm::dwarf::Attribute> seen_attrs;
  return DecodeInternal(die, kMaxAbstractOriginRefsToFollow, &seen_attrs);
}

bool DwarfDieDecoder::DecodeInternal(
    const llvm::DWARFDebugInfoEntry& die, int abstract_origin_refs_to_follow,
    std::vector<llvm::dwarf::Attribute>* seen_attrs) {
  // This indicates the abbreviation. Each DIE starts with an abbreviation
  // code.  The is the number that the DWARFAbbreviationDeclaration was derived
  // from above. We have to read it again to skip the offset over the number.
  //
  //  - A zero abbreviation code indicates a null DIE which is used to mark
  //    the end of a sequence of siblings.
  //
  //  - Otherwise this is a tag of an entry in the .debug_abbrev table (each
  //    entry in that table declares its own tag so it's not an index or an
  //    offset). The abbreviation entry indicates the attributes that this
  //    type of DIE contains, plus the data format for each.
  const llvm::DWARFAbbreviationDeclaration* abbrev =
      die.getAbbreviationDeclarationPtr();
  if (!abbrev)
    return false;

  uint32_t offset = die.getOffset();

  // Skip over the abbreviationcode. We don't actually need this (the abbrev
  // pointer above is derived from this) but we need to move offset past it.
  uint32_t abbr_code = extractor_.getULEB128(&offset);
  if (!abbr_code) {
    FXL_NOTREACHED();  // Should have gotten a null abbrev for this above.
    return false;
  }

  // Set when we encounter an abstract origin attribute.
  llvm::DWARFDie abstract_origin;

  for (const llvm::DWARFAbbreviationDeclaration::AttributeSpec& spec :
       abbrev->attributes()) {
    // Set to true when the form_value has been decoded. Otherwise, the value
    // needs to be skipped to advance through the data.
    bool decoded_current = false;
    llvm::DWARFFormValue form_value(spec.Form);

    // Tracks if the current attribute should be looked up and dispatched.
    // This loop doesn't return early so the form_value.skipValue() call at the
    // bottom will be called when necessary (otherwise the loop won't advance).
    bool needs_dispatch = true;

    if (spec.Attr == llvm::dwarf::DW_AT_abstract_origin) {
      // Abtract origins are handled after loop completion. Explicitly don't
      // check for duplicate attributes in this case so we can follow more than
      // one link in the chain.
      form_value.extractValue(extractor_, &offset, unit_->getFormParams(),
                              unit_);
      abstract_origin = DecodeReference(unit_, form_value);
      decoded_current = true;
    } else {
      // Track attributes that we've already seen and don't decode duplicates
      // (most DIEs won't have duplicates, this is for when we recursively
      // underlay values following abstract origins). This is brute-force
      // because the typical number of attributes is small enough that this
      // should be more efficient than a set which requires per-element heap
      // allocations.
      if (std::find(seen_attrs->begin(), seen_attrs->end(), spec.Attr) !=
          seen_attrs->end())
        needs_dispatch = false;
      else
        seen_attrs->push_back(spec.Attr);
    }

    if (needs_dispatch) {
      // Check for a handler for this attribute and dispatch it.
      for (const Dispatch& dispatch : attrs_) {
        if (spec.Attr != dispatch.first)
          continue;

        // Found the attribute, dispatch it and mark it read.
        if (!decoded_current) {
          form_value.extractValue(extractor_, &offset, unit_->getFormParams(),
                                  unit_);
          decoded_current = true;
        }
        dispatch.second(form_value);
        break;
      }
    }

    if (!decoded_current) {
      // When the attribute wasn't read, skip over it to go to the next.
      form_value.skipValue(extractor_, &offset, unit_->getFormParams());
    }
  }

  // Recursively decode abstract origins. The attributes on the abstract origin
  // DIE "underlay" any attributes present on the current one.
  if (abstract_origin.isValid() && abstract_origin_refs_to_follow > 0) {
    return DecodeInternal(*abstract_origin.getDebugInfoEntry(),
                          abstract_origin_refs_to_follow - 1, seen_attrs);
  } else {
    // The deepest DIE in the abstract origin chain was found (which will be
    // the original DIE itself if there was no abstract origin).
    if (abstract_parent_)
      *abstract_parent_ = unit_->getParent(&die);
  }

  return true;
}

}  // namespace zxdb
