//===-- VLisp.cpp - VLisp compiler ------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "VLisp.h"
#include "llvm/ADT/APInt.h"
#include "llvm/Analysis/CFGPrinter.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Casting.h"
#include <cstdlib>
#include <iostream>

using namespace llvm;

class Environment;
class SyntaxNode;
typedef std::string String;
typedef std::shared_ptr< SyntaxNode > SyntaxNodePtr;
template< typename T > using Array = std::vector < T >;
template< typename T, typename V > using Pair = std::pair < T, V >;
template< typename T, typename V > using Map = std::unordered_map < T, V >;

enum class TokenType
{
  NAME, STRING, LP, RP, I32, F32
};

struct Token
{
  TokenType type;
  char const *pStart, *pEnd;
  float f32;
  int32_t i32;
};

raw_ostream &operator<<(raw_ostream &out, Token const &token) {
  switch (token.type) {
  case TokenType::LP:
    out << "[LP]";
    break;
  case TokenType::RP:
    out << "[RP]";
    break;
  case TokenType::NAME:
    out << "[NAME " << std::string(token.pStart, token.pEnd) << "]";
    break;
  case TokenType::STRING:
    out << "[STRING \"" << std::string(token.pStart, token.pEnd) << "\"]";
    break;
  case TokenType::I32:
    out << "[I32 " << std::to_string(token.i32) << "]";
    break;
  case TokenType::F32:
    out << "[F32 " << std::to_string(token.f32) << "]";
    break;
  }
  return out;
}

typedef Array< Token > TokenArray;

template< typename T >
static Array< T > operator+=(Array< T > &arr, T const &elem) {
  arr.push_back(elem);
  return arr;
}

template< typename K, typename V >
static Map< K, V > operator+=(Map< K, V > &map, Pair< K, V > const &elem) {
  map[elem.first] = elem.second;
  return map;
}

const char *search(const char *pBegin, const char *pEnd, const char *pPat) {
  for (const char *pIter = pBegin; pIter != pEnd; pIter++) {
    bool match = true;
    int offset = 0;
    const char *pJter = pPat;
    for (; *pJter && pIter + offset != pEnd; pJter++, offset++) {
      if (*pJter != pIter[offset]) {
        match = false;
        break;
      }
    }
    if (match && !*pJter) {
      return pIter;
    }
  }
  return nullptr;
}

TokenArray tokenize(char const *pText) {
  char const *pStart = pText;
  char const *pEnd = pText;
  TokenArray outArray;
  auto appendText = [&]() {
    if (pStart != pEnd) {
      if (char const *pC = search(pStart, pEnd, "f32")) {
        assert(pC == pEnd - 3 && "f32 should be at the end");
        char *pEndPtr = nullptr;
        float f = strtof(pStart, &pEndPtr);
        assert(pEndPtr == pEnd - 3 && "error parsing f32");
        outArray += { TokenType::F32, pStart, pEndPtr, f };
      } else if (char const *pC = search(pStart, pEnd, "i32")) {
        assert(pC == pEnd - 3 && "i32 should be at the end");
        char *pEndPtr = nullptr;
        int32_t i = strtol(pStart, &pEndPtr, 10);
        assert(pEndPtr == pEnd - 3 && "error parsing i32");
        outArray += { TokenType::I32, pStart, pEndPtr, 0.0f, i };
      } else {
        outArray += { TokenType::NAME, pStart, pEnd };
      }
    }
  };
  bool inString = false;
  while (*pEnd) {
    switch (*pEnd) {
    case ' ':
    case '\n':
    case '\r':
    case '\t':
      if (inString)
        break;
      appendText();
      pStart = pEnd + 1;
      break;
    case '(':
      if (inString)
        break;
      appendText();
      outArray += { TokenType::LP, pEnd, pEnd + 1 };
      pStart = pEnd + 1;
      break;
    case ')':
      if (inString)
        break;
      appendText();
      outArray += { TokenType::RP, pEnd, pEnd + 1 };
      pStart = pEnd + 1;
      break;
    case '"':
      if (inString) {
        outArray += { TokenType::STRING, pStart, pEnd };
        inString = false;
      } else {
        appendText();
        inString = true;
      }
      pStart = pEnd + 1;
      break;
    default:
      break;
    }
    pEnd++;
  }
  assert(!inString && "unmatched quotes");
  return outArray;
}

struct ListNode
{
  bool isList;
  Token token;
  Array<std::unique_ptr<ListNode>> child;
};

raw_ostream &operator<<(raw_ostream &out, ListNode const *node) {
  if (node->isList)     {
    out << "( ";
    for (auto const &child : node->child) {
      out << child.get() << " ";
    }
    out << ")";
  } else {
    out << "*" << node->token;
  }
  return out;
}

std::unique_ptr<ListNode> getLists(TokenArray const &tokens) {
  std::unique_ptr<ListNode> root(new ListNode);
  root->isList = true;
  Array<ListNode*> nodeStack;
  ListNode *curNode = root.get();
  for (auto const &token : tokens) {
    switch (token.type) {
    case TokenType::LP:
    {
      auto *newNode = new ListNode;
      curNode->child.emplace_back(newNode);
      nodeStack.push_back(curNode);
      curNode = newNode;
    }
      break;
    case TokenType::RP:
    {
      assert(nodeStack.size() && "unbalanced parentheses");
      curNode = nodeStack.back();
      nodeStack.pop_back();
    }
      break;
    default:
    {
      curNode->child.emplace_back(new ListNode{ false, token });
    }
      break;
    }
  }
  return root;
}

enum class AstType
{
  LIST, CALL, STRING, I32, F32
};

enum class ExprType
{
  STRING, I32, F32, LIST
};

class TAstNode;

struct AstNode
{
  AstType astType;
  Token token;
  Array<std::unique_ptr<AstNode>> child;
  std::unique_ptr<TAstNode> get();
};

struct TAstNode
{
  ExprType expType;
  AstType astType;
  Type type;
  Token token;
  Array<std::unique_ptr<AstNode>> child;
};


Module *parse(std::istream &input, LLVMContext &ctx,
  raw_ostream *out, bool dumpAst)   {

  std::string const source((std::istreambuf_iterator<char>(input)),
    std::istreambuf_iterator<char>());

  auto tokens = tokenize(source.c_str());
  auto listRoot = getLists(tokens);
  if (dumpAst) {
    for (auto const &token : tokens) {
      *out << token << " ";
    }
    *out << "\n";
    *out << listRoot.get() << "\n";
		//DisplayGraph(Filename, false, GraphProgram::DOT);
  }


  auto *module = new Module("VLisp", ctx);
  auto *putsFunc = cast<Function>(module->
    getOrInsertFunction("puts", IntegerType::getInt32Ty(ctx),
      PointerType::getUnqual(IntegerType::getInt8Ty(ctx))));
  auto *vlispFunc = cast<Function>(module->
    getOrInsertFunction("vlisp", Type::getVoidTy(ctx)));
  std::unique_ptr<IRBuilder<>> builder(new IRBuilder<>(BasicBlock::Create(ctx, "vlistp", vlispFunc)));
  BasicBlock* BB = builder->GetInsertBlock();
  Constant *msg =
    ConstantDataArray::getString(ctx, "Static string!",
      true);
  GlobalVariable *msgGlob = new GlobalVariable(
    *module,
    msg->getType(),
    true,
    GlobalValue::InternalLinkage,
    msg,
    "staticString");
  Constant *zero_32 = Constant::getNullValue(IntegerType::getInt32Ty(ctx));
  Constant *gep_params[] = {
    zero_32,
    zero_32
  };
  Constant *msgptr = ConstantExpr::
    getGetElementPtr(msgGlob->getValueType(), msgGlob, gep_params);
  Value *puts_params[] = {
    msgptr
  };
  CallInst *putsCall =
    CallInst::Create(putsFunc,
      puts_params,
      "", BB);
  ReturnInst::Create(ctx, BB);
  return module;
}