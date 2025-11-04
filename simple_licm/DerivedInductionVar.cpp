#include "llvm/IR/PassManager.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Transforms/Utils/ScalarEvolutionExpander.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

static PHINode *findPrimaryIV(Loop &L, ScalarEvolution &SE) {
  BasicBlock *H = L.getHeader();
  if (!H) return nullptr;
  for (PHINode &PN : H->phis()) {
    if (!PN.getType()->isIntegerTy()) continue;
    const SCEV *S = SE.getSCEV(&PN);
    if (const auto *AR = dyn_cast<SCEVAddRecExpr>(S)) {
      if (AR->getLoop() != &L || !AR->isAffine()) continue;
      if (const auto *Step = dyn_cast<SCEVConstant>(AR->getStepRecurrence(SE))) {
        APInt Abs = Step->getAPInt().abs();
        if (Abs.eq(APInt(Abs.getBitWidth(), 1))) return &PN;
      }
    }
  }
  return nullptr;
}

static bool isDerivedIV(Loop &L, PHINode &PN, ScalarEvolution &SE) {
  if (!PN.getType()->isIntegerTy()) return false;
  const SCEV *S = SE.getSCEV(&PN);
  if (const auto *AR = dyn_cast<SCEVAddRecExpr>(S)) {
    return AR->getLoop() == &L && AR->isAffine();
  }
  return false;
}

struct DerivedInductionVarAnalysis : PassInfoMixin<DerivedInductionVarAnalysis> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM) {
    auto &LI = AM.getResult<LoopAnalysis>(F);
    auto &SE = AM.getResult<ScalarEvolutionAnalysis>(F);
    for (Loop *L : LI) {
      if (!L || !L->getSubLoops().empty()) continue; // inner loops only
      PHINode *Primary = findPrimaryIV(*L, SE);
      errs() << "Function " << F.getName() << " inner loop\n";
      if (Primary) errs() << "  primary-iv: " << Primary->getName() << "\n";
      if (BasicBlock *H = L->getHeader()) {
        for (PHINode &PN : H->phis()) {
          if (&PN == Primary) continue;
          if (isDerivedIV(*L, PN, SE)) errs() << "  derived-iv: " << PN.getName() << "\n";
        }
      }
    }
    return PreservedAnalyses::all();
  }
};

struct DerivedInductionVarElim : PassInfoMixin<DerivedInductionVarElim> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM) {
    auto &LI = AM.getResult<LoopAnalysis>(F);
    auto &SE = AM.getResult<ScalarEvolutionAnalysis>(F);
    const DataLayout &DL = F.getParent()->getDataLayout();
    bool Changed = false;

    for (Loop *L : LI) {
      if (!L || !L->getSubLoops().empty()) continue; // inner loops only
      BasicBlock *H = L->getHeader();
      if (!H) continue;

      PHINode *Primary = findPrimaryIV(*L, SE);
      if (!Primary) continue;

      SCEVExpander Exp(SE, DL, "ive");
      SmallVector<PHINode*, 8> Dead;

      for (PHINode &PN : H->phis()) {
        if (&PN == Primary) continue;
        if (!isDerivedIV(*L, PN, SE)) continue;

        SmallVector<Use*, 16> Uses;
        for (Use &U : PN.uses()) Uses.push_back(&U);

        for (Use *U : Uses) {
          Instruction *UserI = dyn_cast<Instruction>(U->getUser());
          if (!UserI) continue;
          const SCEV *S = SE.getSCEV(&PN);
          Value *NewV = Exp.expandCodeFor(S, PN.getType(), UserI);
          if (NewV && NewV != &PN) {
            U->set(NewV);
            Changed = true;
          }
        }
        if (PN.use_empty()) Dead.push_back(&PN);
      }

      for (PHINode *D : Dead) {
        D->eraseFromParent();
        Changed = true;
      }
    }

    return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }
};

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {
    LLVM_PLUGIN_API_VERSION,
    "DerivedInductionVar",
    LLVM_VERSION_STRING,
    [](PassBuilder &PB) {
      PB.registerPipelineParsingCallback(
        [](StringRef Name, FunctionPassManager &FPM,
           ArrayRef<PassBuilder::PipelineElement>) {
          if (Name == "derived-iv") { FPM.addPass(DerivedInductionVarAnalysis()); return true; }
          if (Name == "derived-iv-elim") { FPM.addPass(DerivedInductionVarElim()); return true; }
          return false;
        });
    }
  };
}
