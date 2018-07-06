//===-- VLisp.h - VLisp compiler class ------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

#include <istream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using namespace llvm;

Module *parse(std::istream &in1, LLVMContext& C,
  raw_ostream *out, bool dumpAst);