// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/elflib/elflib.h"

#include <limits.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include <algorithm>
#include <iterator>

#include "gtest/gtest.h"

namespace elflib {
namespace {

constexpr uint64_t kAddrPoison = 0xdeadb33ff00db4b3;
constexpr uint64_t kSymbolPoison = 0xb0bab0ba;
constexpr uint64_t kNoteGnuBuildId = 3;
constexpr uint64_t kMeaninglessNoteType = 42;

std::string GetSelfPath() {
  std::string result;
#if defined(__APPLE__)
  // Executable path can have relative references ("..") depending on how the
  // app was launched.
  uint32_t length = 0;
  _NSGetExecutablePath(nullptr, &length);
  result.resize(length);
  _NSGetExecutablePath(&result[0], &length);
  result.resize(length - 1);  // Length included terminator.
#elif defined(__linux__)
  // The realpath() call below will resolve the symbolic link.
  result.assign("/proc/self/exe");
#else
#error Write this for your platform.
#endif

  char fullpath[PATH_MAX];
  return std::string(realpath(result.c_str(), fullpath));
}

inline std::string GetTestFilePath(const std::string& rel_path) {
  std::string path = GetSelfPath();
  size_t last_slash = path.rfind('/');
  if (last_slash == std::string::npos) {
    path = "./";  // Just hope the current directory works.
  } else {
    path.resize(last_slash + 1);
  }
  return path + rel_path;
}

// The test files will be copied over to this specific location at build time.
constexpr char kRelativeTestDataPath[] = "test_data/elflib/";
constexpr char kStrippedExampleFile[] = "stripped_example.elf";
constexpr char kUnstrippedExampleFile[] = "unstripped_example.elf";

inline std::string GetTestBinaryPath(const std::string& bin) {
  return GetTestFilePath(kRelativeTestDataPath) + bin;
}

class TestData {
 public:
  TestData() {
    PushData(Elf64_Ehdr{
        .e_ident = {0, 0, 0, 0, ELFCLASS64, ELFDATA2LSB, EV_CURRENT},
        .e_version = EV_CURRENT,
        .e_shoff = sizeof(Elf64_Ehdr),
        .e_ehsize = sizeof(Elf64_Ehdr),
        .e_shentsize = sizeof(Elf64_Shdr),
        .e_phentsize = sizeof(Elf64_Phdr),
        .e_shnum = 4,
        .e_phnum = 2,
        .e_shstrndx = 0,
    });

    *DataAt<char>(0) = ElfMagic[0];
    *DataAt<char>(1) = ElfMagic[1];
    *DataAt<char>(2) = ElfMagic[2];
    *DataAt<char>(3) = ElfMagic[3];

    size_t shstrtab_hdr = PushData(Elf64_Shdr{
        .sh_name = 1,
        .sh_type = SHT_STRTAB,
        .sh_size = 34,
        .sh_addr = kAddrPoison,
    });
    size_t stuff_hdr = PushData(Elf64_Shdr{
        .sh_name = 11,
        .sh_type = SHT_LOUSER,
        .sh_size = 15,
        .sh_addr = kAddrPoison,
    });
    size_t strtab_hdr = PushData(Elf64_Shdr{
        .sh_name = 18,
        .sh_type = SHT_STRTAB,
        .sh_size = 16,
        .sh_addr = kAddrPoison,
    });
    size_t symtab_hdr = PushData(Elf64_Shdr{
        .sh_name = 26,
        .sh_type = SHT_SYMTAB,
        .sh_size = sizeof(Elf64_Sym),
        .sh_addr = kAddrPoison,
    });

    size_t phnote_hdr = PushData(Elf64_Phdr{
        .p_type = PT_NOTE,
        .p_vaddr = kAddrPoison,
    });
    DataAt<Elf64_Ehdr>(0)->e_phoff = phnote_hdr;

    DataAt<Elf64_Shdr>(shstrtab_hdr)->sh_offset =
        PushData("\0.shstrtab\0.stuff\0.strtab\0.symtab\0", 34);

    DataAt<Elf64_Shdr>(stuff_hdr)->sh_offset = PushData("This is a test.", 15);

    DataAt<Elf64_Shdr>(strtab_hdr)->sh_offset =
        PushData("\0zx_frob_handle\0", 16);

    DataAt<Elf64_Shdr>(symtab_hdr)->sh_offset = PushData(Elf64_Sym{
        .st_name = 1,
        .st_shndx = SHN_COMMON,
        .st_value = kSymbolPoison,
        .st_size = 0,
    });

    size_t buildid_nhdr = PushData(
        Elf64_Nhdr{.n_namesz = 4, .n_descsz = 32, .n_type = kNoteGnuBuildId});

    DataAt<Elf64_Phdr>(phnote_hdr)->p_offset = buildid_nhdr;

    PushData("GNU\0", 4);

    uint8_t desc_data[32] = {
        0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3, 4, 5, 6, 7,
        0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3, 4, 5, 6, 7,
    };

    PushData(desc_data, 32);

    PushData(Elf64_Nhdr{
        .n_namesz = 6,
        .n_descsz = 3,
        .n_type = kMeaninglessNoteType,
    });

    PushData("seven\0\0\0", 8);
    PushData("foo\0", 4);

    DataAt<Elf64_Phdr>(phnote_hdr)->p_filesz = Pos() - buildid_nhdr;
    DataAt<Elf64_Phdr>(phnote_hdr)->p_memsz = Pos() - buildid_nhdr;
  }

  template <typename T>
  T* DataAt(size_t offset) {
    return reinterpret_cast<T*>(content_.data() + offset);
  }

  template <typename T>
  size_t PushData(T data) {
    return PushData(reinterpret_cast<uint8_t*>(&data), sizeof(data));
  }

  size_t PushData(const char* bytes, size_t size) {
    return PushData(reinterpret_cast<const uint8_t*>(bytes), size);
  }

  size_t PushData(const uint8_t* bytes, size_t size) {
    size_t offset = Pos();
    std::copy(bytes, bytes + size, std::back_inserter(content_));
    return offset;
  }

  size_t Pos() { return content_.size(); }
  size_t Size() { return content_.size(); }
  const uint8_t* Data() { return content_.data(); }

  std::function<bool(uint64_t, std::vector<uint8_t>*)> GetFetcher() {
    return [this](uint64_t offset, std::vector<uint8_t>* out) {
      if (offset + out->size() > content_.size()) {
        return false;
      }

      std::copy(content_.begin() + offset,
                content_.begin() + offset + out->size(), out->begin());
      return true;
    };
  }

 private:
  std::vector<uint8_t> content_;
};

}  // namespace

TEST(ElfLib, Create) {
  TestData t;
  std::unique_ptr<ElfLib> got;

  EXPECT_NE(ElfLib::Create(t.Data(), t.Size()).get(), nullptr);
}

TEST(ElfLib, GetSection) {
  TestData t;
  std::unique_ptr<ElfLib> elf = ElfLib::Create(t.Data(), t.Size());

  ASSERT_NE(elf.get(), nullptr);

  auto data = elf->GetSectionData(".stuff");
  const uint8_t* expected_content =
      reinterpret_cast<const uint8_t*>("This is a test.");

  ASSERT_NE(data.ptr, nullptr);

  auto expect = std::vector<uint8_t>(expected_content, expected_content + 15);
  auto got = std::vector<uint8_t>(data.ptr, data.ptr + data.size);

  EXPECT_EQ(expect, got);
}

TEST(ElfLib, GetSymbolValue) {
  TestData t;
  std::unique_ptr<ElfLib> elf = ElfLib::Create(t.Data(), t.Size());

  ASSERT_NE(elf.get(), nullptr);

  auto data = elf->GetSymbol("zx_frob_handle");
  ASSERT_TRUE(data);
  EXPECT_EQ(kSymbolPoison, data->st_value);
}

TEST(ElfLib, GetAllSymbols) {
  TestData t;
  std::unique_ptr<ElfLib> elf = ElfLib::Create(t.Data(), t.Size());

  ASSERT_NE(elf.get(), nullptr);

  auto syms = elf->GetAllSymbols();
  ASSERT_TRUE(syms);
  EXPECT_EQ(1U, syms->size());

  Elf64_Sym sym = (*syms)["zx_frob_handle"];
  EXPECT_EQ(1U, sym.st_name);
  EXPECT_EQ(0U, sym.st_size);
  EXPECT_EQ(SHN_COMMON, sym.st_shndx);
  EXPECT_EQ(kSymbolPoison, sym.st_value);
}

TEST(ElfLib, GetNote) {
  TestData t;
  std::unique_ptr<ElfLib> elf = ElfLib::Create(t.Data(), t.Size());

  ASSERT_NE(elf.get(), nullptr);

  auto got = elf->GetNote("GNU", kNoteGnuBuildId);

  EXPECT_TRUE(got);
  auto data = *got;

  EXPECT_EQ(32U, data.size());

  for (size_t i = 0; i < 32; i++) {
    EXPECT_EQ(i % 8, data[i]);
  }
}

TEST(ElfLib, GetIrregularNote) {
  TestData t;
  std::unique_ptr<ElfLib> elf = ElfLib::Create(t.Data(), t.Size());

  ASSERT_NE(elf.get(), nullptr);

  auto got = elf->GetNote("seven", kMeaninglessNoteType);

  EXPECT_TRUE(got);
  auto data = *got;

  EXPECT_EQ(3U, data.size());

  EXPECT_EQ("foo", std::string(data.data(), data.data() + 3));
}

TEST(ElfLib, GetSymbolsFromStripped) {
  std::unique_ptr<ElfLib> elf =
      ElfLib::Create(GetTestBinaryPath(kStrippedExampleFile));

  ASSERT_NE(elf.get(), nullptr);

  auto missing_syms = elf->GetAllSymbols();
  EXPECT_FALSE(missing_syms);

  auto syms = elf->GetAllDynamicSymbols();
  ASSERT_TRUE(syms);
  EXPECT_EQ(8U, syms->size());

  std::map<std::string, Elf64_Sym>::iterator it;

  it = syms->find("");
  EXPECT_NE(it, syms->end());
  it = syms->find("__bss_start");
  EXPECT_NE(it, syms->end());
  it = syms->find("__libc_start_main");
  EXPECT_NE(it, syms->end());
  it = syms->find("__scudo_default_options");
  EXPECT_NE(it, syms->end());
  it = syms->find("_edata");
  EXPECT_NE(it, syms->end());
  it = syms->find("_end");
  EXPECT_NE(it, syms->end());
  it = syms->find("printf");
  EXPECT_NE(it, syms->end());
  it = syms->find("strlen");
  EXPECT_NE(it, syms->end());
}

TEST(ElfLib, GetPLTFromUnstripped) {
  std::unique_ptr<ElfLib> elf =
      ElfLib::Create(GetTestBinaryPath(kUnstrippedExampleFile));

  ASSERT_NE(elf.get(), nullptr);

  auto plt = elf->GetPLTOffsets();

  EXPECT_EQ(261U, plt.size());

  // We won't check every entry, but we'll grab a few representative ones.
  EXPECT_EQ(0x1a4df0U, plt["_ZN3fxl10LogMessageC1EiPKciS2_"]);
  EXPECT_EQ(0x1a4e50U, plt["_ZNSt3__218condition_variableD1Ev"]);
  EXPECT_EQ(0x1a5a00U, plt["vprintf"]);
  EXPECT_EQ(0x1a52b0U, plt["zx_channel_write"]);
}

}  // namespace elflib
