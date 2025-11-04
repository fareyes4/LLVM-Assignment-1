#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

static bool isPureRegOp(const Instruction &I) {
  if (I.isTerminator()) return false;
  if (isa<PHINode>(&I)) return false;
  if (I.mayReadOrWriteMemory()) return false;
  return true;
}
static bool operandIsLoopInvariant(Value *V, const Loop *L) {
  if (isa<Constant>(V)) return true;
  if (auto *I = dyn_cast<Instruction>(V)) return !L->contains(I);
  return true;
}

struct SimpleLICMLoop : PassInfoMixin<SimpleLICMLoop> {
  PreservedAnalyses run(Loop &L, LoopAnalysisManager &,
                        LoopStandardAnalysisResults &, LPMUpdater &) {
    BasicBlock *Preheader = L.getLoopPreheader();
    if (!Preheader) return PreservedAnalyses::all();
    Instruction *InsertPt = Preheader->getTerminator();

    SmallPtrSet<Instruction*, 32> Hoisted;
    bool Changed = false, Progress = true;
    while (Progress) {
      Progress = false;
      for (BasicBlock *BB : L.blocks()) {
        for (Instruction &I : *BB) {
          if (!isPureRegOp(I) || Hoisted.contains(&I)) continue;
          bool Invariant = true;
          for (Value *Op : I.operands())
            if (!operandIsLoopInvariant(Op, &L)) { Invariant = false; break; }
          if (!Invariant) continue;
          errs() << "Hoisting: " << I << "\n";
          I.moveBefore(InsertPt);
          Hoisted.insert(&I);
          Changed = true; Progress = true;
        }
      }
    }
    return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }
};

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "simple-licm", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
              [](StringRef Name, LoopPassManager &LPM,
                 ArrayRef<PassBuilder::PipelineElement>) {
                if (Name == "simple-licm") { LPM.addPass(SimpleLICMLoop()); return true; }
                return false;
              });
          }};
}

