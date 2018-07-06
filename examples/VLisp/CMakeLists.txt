set(LLVM_LINK_COMPONENTS
  BitWriter
  Core
  ExecutionEngine
  MC
  MCJIT
  Support
  nativecodegen
  )

add_llvm_example(VLisp
  VLisp.cpp
  VLisp.h
  VLispDriver.cpp
  )
