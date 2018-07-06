//===-- VLispDriver.cpp - VLisp compiler driver -------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//


#include "VLisp.h"
#include "llvm/ADT/APInt.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/ExecutionEngine/MCJIT.h"
#include "llvm/IR/AssemblyAnnotationWriter.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

using namespace llvm;

//Command line options

static cl::opt<std::string>
InputFilename(cl::Positional, cl::desc("<input filename>"));

static cl::opt<std::string>
OutputFilename("o", cl::desc("Output filename"), cl::value_desc("filename"));

static cl::opt<bool>
AST("ast", cl::desc("print AST"));

static cl::opt<bool>
TAST("tast", cl::desc("print typed AST"));

static cl::opt<bool>
PrintTokens("tokens", cl::desc("print tokens"));

static cl::opt<bool>
PrintLists("list", cl::desc("print raw lists"));

static void printDebugLoc(const DebugLoc &DL, formatted_raw_ostream &OS) {
  OS << DL.getLine() << ":" << DL.getCol();
  if (DILocation *IDL = DL.getInlinedAt()) {
    OS << "@";
    printDebugLoc(IDL, OS);
  }
}
class CommentWriter : public AssemblyAnnotationWriter {
public:
  void emitFunctionAnnot(const Function *F,
    formatted_raw_ostream &OS) override {
    OS << "; [#uses=" << F->getNumUses() << ']';  // Output # uses
    OS << '\n';
  }
  void printInfoComment(const Value &V, formatted_raw_ostream &OS) override {
    bool Padded = false;
    if (!V.getType()->isVoidTy()) {
      OS.PadToColumn(50);
      Padded = true;
      // Output # uses and type
      OS << "; [#uses=" << V.getNumUses() << " type=" << *V.getType() << "]";
    }
    if (const Instruction *I = dyn_cast<Instruction>(&V)) {
      if (const DebugLoc &DL = I->getDebugLoc()) {
        if (!Padded) {
          OS.PadToColumn(50);
          Padded = true;
          OS << ";";
        }
        OS << " [debug line = ";
        printDebugLoc(DL, OS);
        OS << "]";
      }
      if (const DbgDeclareInst *DDI = dyn_cast<DbgDeclareInst>(I)) {
        if (!Padded) {
          OS.PadToColumn(50);
          OS << ";";
        }
        OS << " [debug variable = " << DDI->getVariable()->getName() << "]";
      } else if (const DbgValueInst *DVI = dyn_cast<DbgValueInst>(I)) {
        if (!Padded) {
          OS.PadToColumn(50);
          OS << ";";
        }
        OS << " [debug variable = " << DVI->getVariable()->getName() << "]";
      }
    }
  }
};


int main(int argc, char **argv) {
  cl::ParseCommandLineOptions(argc, argv, "VLisp compiler\n");

  LLVMContext Context;

  assert(InputFilename != "" && "Please provide an input filename");

  if (OutputFilename == "")
    OutputFilename = "a.ll";
  std::error_code EC;
 
  std::ifstream in(InputFilename.c_str());

  std::unique_ptr<Module> mod(parse(in, Context, &llvm::errs(), true));
  // insert int main(int argc, char **argv) { vlisp(); return 0; }
  {
    Function *main_func = cast<Function>(mod->
      getOrInsertFunction("main", IntegerType::getInt32Ty(mod->getContext()),
        IntegerType::getInt32Ty(mod->getContext()),
        PointerType::getUnqual(PointerType::getUnqual(
          IntegerType::getInt8Ty(mod->getContext())))));
    {
      Function::arg_iterator args = main_func->arg_begin();
      Value *arg_0 = &*args++;
      arg_0->setName("argc");
      Value *arg_1 = &*args++;
      arg_1->setName("argv");
    }

    BasicBlock *bb = BasicBlock::Create(mod->getContext(), "main.0", main_func);

    {
      CallInst *brainf_call = CallInst::Create(mod->getFunction("vlisp"),
        "", bb);
      brainf_call->setTailCall(false);
    }
    ReturnInst::Create(mod->getContext(),
      ConstantInt::get(mod->getContext(), APInt(32, 0)), bb);
  }

  assert(!verifyModule(*mod, &llvm::errs()) && "llvm module verification failed");

  raw_fd_ostream out(OutputFilename, EC, sys::fs::F_None);
  std::unique_ptr<AssemblyAnnotationWriter> Annotator(new CommentWriter());
  mod->print(out, Annotator.get(), true);
  llvm_shutdown();

  return 0;
}
