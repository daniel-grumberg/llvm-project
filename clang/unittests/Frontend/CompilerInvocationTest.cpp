//===- unittests/Frontend/CompilerInvocationTest.cpp - CI tests //---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/CompilerInstance.h"
#include "gtest/gtest.h"

using namespace llvm;
using namespace clang;

namespace {

TEST(CompilerInvocation, CanGenerateCC1CommandLine) {
  const char *Args[] = {"clang", "-xc++", "-fmodules-strict-context-hash", "-"};

  IntrusiveRefCntPtr<DiagnosticsEngine> Diags =
      CompilerInstance::createDiagnostics(new DiagnosticOptions());

  CompilerInvocation CInvok;
  CompilerInvocation::CreateFromArgs(CInvok, Args, *Diags);

  SmallVector<const char *, 32> GeneratedArgs;
  SmallVector<std::string, 32> GeneratedArgsStorage;
  auto StringAlloc = [&GeneratedArgsStorage](const Twine &Arg) {
    return GeneratedArgsStorage.emplace_back(Arg.str()).c_str();
  };

  CInvok.generateCC1CommandLine(GeneratedArgs, StringAlloc);

  ASSERT_STREQ(GeneratedArgs[0], "-fmodules-strict-context-hash");
}

} // anonymous namespace
