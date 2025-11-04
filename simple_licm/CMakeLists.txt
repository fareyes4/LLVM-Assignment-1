add_llvm_library(SimpleLICM MODULE
  SimpleLICM.cpp
  AffineRecurrence.cpp
  DerivedInductionVar.cpp
  PLUGIN_TOOL
  opt
)
