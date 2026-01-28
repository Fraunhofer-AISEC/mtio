#include "RISCV.h"
#include "RISCVInstrInfo.h"
#include "RISCVSubtarget.h"
#include "RISCVTargetMachine.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/StackSafetyAnalysis.h"
#include "llvm/CodeGen/LiveRegUnits.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/MemoryTaggingSupport.h"
#include <cassert>
#include <iterator>
#include <memory>
#include <unistd.h>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "riscv-stack-tagging"

static cl::opt<bool> ClMergeInit(
    "stack-tagging-merge-init", cl::Hidden, cl::init(false),
    cl::desc("merge stack variable initializers with tagging when possible"));

static cl::opt<bool>
    ClUseStackSafety("stack-tagging-use-stack-safety", cl::Hidden,
                     cl::init(true),
                     cl::desc("Use Stack Safety analysis results"));

static cl::opt<unsigned> ClScanLimit("stack-tagging-merge-init-scan-limit",
                                     cl::init(40), cl::Hidden);

static cl::opt<unsigned>
    ClMergeInitSizeLimit("stack-tagging-merge-init-size-limit", cl::init(272),
                         cl::Hidden);

static cl::opt<size_t> ClMaxLifetimes(
    "stack-tagging-max-lifetimes-for-alloca", cl::Hidden, cl::init(3),
    cl::ReallyHidden,
    cl::desc("How many lifetime ends to handle for a single alloca."),
    cl::Optional);

static Align kTagGranuleSize = Align(16);
// static Align kTagGranuleSize = Align(1);

namespace {

class InitializerBuilder {
  uint64_t Size;
  const DataLayout *DL;
  Value *BasePtr;
  Function *SetTagFn;
  Function *SetTagZeroFn;
  Function *StgpFn;

  // List of initializers sorted by start offset.
  struct Range {
    uint64_t Start, End;
    Instruction *Inst;
  };
  SmallVector<Range, 4> Ranges;
  // 8-aligned offset => 8-byte initializer
  // Missing keys are zero initialized.
  std::map<uint64_t, Value *> Out;

public:
  InitializerBuilder(uint64_t Size, const DataLayout *DL, Value *BasePtr,
                     Function *SetTagFn, Function *SetTagZeroFn,
                     Function *StgpFn)
      : Size(Size), DL(DL), BasePtr(BasePtr), SetTagFn(SetTagFn),
        SetTagZeroFn(SetTagZeroFn), StgpFn(StgpFn) {}

  bool addRange(uint64_t Start, uint64_t End, Instruction *Inst) {
    auto I =
        llvm::lower_bound(Ranges, Start, [](const Range &LHS, uint64_t RHS) {
          return LHS.End <= RHS;
        });
    if (I != Ranges.end() && End > I->Start) {
      // Overlap - bail.
      return false;
    }
    Ranges.insert(I, {Start, End, Inst});
    return true;
  }

  bool addStore(uint64_t Offset, StoreInst *SI, const DataLayout *DL) {
    int64_t StoreSize = DL->getTypeStoreSize(SI->getOperand(0)->getType());
    if (!addRange(Offset, Offset + StoreSize, SI))
      return false;
    IRBuilder<> IRB(SI);
    applyStore(IRB, Offset, Offset + StoreSize, SI->getOperand(0));
    return true;
  }

  bool addMemSet(uint64_t Offset, MemSetInst *MSI) {
    uint64_t StoreSize = cast<ConstantInt>(MSI->getLength())->getZExtValue();
    if (!addRange(Offset, Offset + StoreSize, MSI))
      return false;
    IRBuilder<> IRB(MSI);
    applyMemSet(IRB, Offset, Offset + StoreSize,
                cast<ConstantInt>(MSI->getValue()));
    return true;
  }

  void applyMemSet(IRBuilder<> &IRB, int64_t Start, int64_t End,
                   ConstantInt *V) {
    // Out[] does not distinguish between zero and undef, and we already know
    // that this memset does not overlap with any other initializer. Nothing to
    // do for memset(0).
    if (V->isZero())
      return;
    for (int64_t Offset = Start - Start % 8; Offset < End; Offset += 8) {
      uint64_t Cst = 0x0101010101010101UL;
      int LowBits = Offset < Start ? (Start - Offset) * 8 : 0;
      if (LowBits)
        Cst = (Cst >> LowBits) << LowBits;
      int HighBits = End - Offset < 8 ? (8 - (End - Offset)) * 8 : 0;
      if (HighBits)
        Cst = (Cst << HighBits) >> HighBits;
      ConstantInt *C =
          ConstantInt::get(IRB.getInt64Ty(), Cst * V->getZExtValue());

      Value *&CurrentV = Out[Offset];
      if (!CurrentV) {
        CurrentV = C;
      } else {
        CurrentV = IRB.CreateOr(CurrentV, C);
      }
    }
  }

  // Take a 64-bit slice of the value starting at the given offset (in bytes).
  // Offset can be negative. Pad with zeroes on both sides when necessary.
  Value *sliceValue(IRBuilder<> &IRB, Value *V, int64_t Offset) {
    if (Offset > 0) {
      V = IRB.CreateLShr(V, Offset * 8);
      V = IRB.CreateZExtOrTrunc(V, IRB.getInt64Ty());
    } else if (Offset < 0) {
      V = IRB.CreateZExtOrTrunc(V, IRB.getInt64Ty());
      V = IRB.CreateShl(V, -Offset * 8);
    } else {
      V = IRB.CreateZExtOrTrunc(V, IRB.getInt64Ty());
    }
    return V;
  }

  void applyStore(IRBuilder<> &IRB, int64_t Start, int64_t End,
                  Value *StoredValue) {
    StoredValue = flatten(IRB, StoredValue);
    for (int64_t Offset = Start - Start % 8; Offset < End; Offset += 8) {
      Value *V = sliceValue(IRB, StoredValue, Offset - Start);
      Value *&CurrentV = Out[Offset];
      if (!CurrentV) {
        CurrentV = V;
      } else {
        CurrentV = IRB.CreateOr(CurrentV, V);
      }
    }
  }

  void generate(IRBuilder<> &IRB) {
    LLVM_DEBUG(dbgs() << "Combined initializer\n");
    // No initializers => the entire allocation is undef.
    if (Ranges.empty()) {
      emitUndef(IRB, 0, Size);
      return;
    }

    // Look through 8-byte initializer list 16 bytes at a time;
    // If one of the two 8-byte halfs is non-zero non-undef, emit STGP.
    // Otherwise, emit zeroes up to next available item.
    uint64_t LastOffset = 0;
    if (LastOffset < Size)
      emitZeroes(IRB, LastOffset, Size - LastOffset);

    for (const auto &R : Ranges) {
      R.Inst->eraseFromParent();
    }
  }

  void emitZeroes(IRBuilder<> &IRB, uint64_t Offset, uint64_t Size) {
    LLVM_DEBUG(dbgs() << "  [" << Offset << ", " << Offset + Size
                      << ") zero\n");
    Value *Ptr = BasePtr;
    if (Offset)
      Ptr = IRB.CreateConstGEP1_32(IRB.getInt8Ty(), Ptr, Offset);
    IRB.CreateCall(SetTagZeroFn,
                   {Ptr, ConstantInt::get(IRB.getInt64Ty(), Size)});
  }

  void emitUndef(IRBuilder<> &IRB, uint64_t Offset, uint64_t Size) {
    LLVM_DEBUG(dbgs() << "  [" << Offset << ", " << Offset + Size
                      << ") undef\n");
    Value *Ptr = BasePtr;
    if (Offset)
      Ptr = IRB.CreateConstGEP1_32(IRB.getInt8Ty(), Ptr, Offset);
    IRB.CreateCall(SetTagFn, {Ptr, ConstantInt::get(IRB.getInt64Ty(), Size)});
  }

  void emitPair(IRBuilder<> &IRB, uint64_t Offset, Value *A, Value *B) {
    LLVM_DEBUG(dbgs() << "  [" << Offset << ", " << Offset + 16 << "):\n");
    LLVM_DEBUG(dbgs() << "    " << *A << "\n    " << *B << "\n");
    Value *Ptr = BasePtr;
    if (Offset)
      Ptr = IRB.CreateConstGEP1_32(IRB.getInt8Ty(), Ptr, Offset);
    IRB.CreateCall(StgpFn, {Ptr, A, B});
  }

  Value *flatten(IRBuilder<> &IRB, Value *V) {
    if (V->getType()->isIntegerTy())
      return V;
    // vector of pointers -> vector of ints
    if (VectorType *VecTy = dyn_cast<VectorType>(V->getType())) {
      LLVMContext &Ctx = IRB.getContext();
      Type *EltTy = VecTy->getElementType();
      if (EltTy->isPointerTy()) {
        uint32_t EltSize = DL->getTypeSizeInBits(EltTy);
        auto *NewTy = FixedVectorType::get(
            IntegerType::get(Ctx, EltSize),
            cast<FixedVectorType>(VecTy)->getNumElements());
        V = IRB.CreatePointerCast(V, NewTy);
      }
    }
    return IRB.CreateBitOrPointerCast(
        V, IRB.getIntNTy(DL->getTypeStoreSize(V->getType()) * 8));
  }
};

class RISCVStackTagging : public FunctionPass {
  // FIXME Manually set to false, avoiding constructor
  const bool MergeInit;
  const bool UseStackSafety;

public:
  static char ID; // Pass ID, replacement for typeid

  RISCVStackTagging(bool IsOptNone = false)
      : FunctionPass(ID),
        MergeInit(ClMergeInit.getNumOccurrences() ? ClMergeInit : !IsOptNone),
        // UseStackSafety(false)
        UseStackSafety(ClUseStackSafety.getNumOccurrences() ? ClUseStackSafety
                                                            : !IsOptNone) {
    initializeRISCVStackTaggingPass(*PassRegistry::getPassRegistry());
  }

  void tagAlloca(AllocaInst *AI, Instruction *InsertBefore, Value *Ptr,
                 uint64_t Size);
  void untagAlloca(AllocaInst *AI, Instruction *InsertBefore, uint64_t Size);
  void instrumentMalloc(CallInst *CI);
  void instrumentCMP(Instruction *CI);
  bool instrumentGEP(GetElementPtrInst *GEP);
  uint8_t colorSize = 4;
  uint8_t modeSize = 3;
  uint8_t shadeSize = 1;

  std::vector<std::tuple<int, int>>
  getMetadataLayoutFromType(StructType *ST, int *currentShade);
  void applyShading(IRBuilder<> &IRB, StructType *type, Value *AI);
  bool containsUnion(StructType *ST);
  bool isAllocationFunction(CallBase *CB);
  Instruction *collectInitializers(Instruction *StartInst, Value *StartPtr,
                                   uint64_t Size, InitializerBuilder &IB);

  Instruction *insertBaseTaggedPointer(
      const MapVector<AllocaInst *, memtag::AllocaInfo> &Allocas,
      const DominatorTree *DT);
  bool runOnFunction(Function &F) override;

  StringRef getPassName() const override { return "RISCV Stack Tagging"; }

private:
  Function *F = nullptr;
  Function *SetTagFunc = nullptr;
  Function *ShadeFunc = nullptr;
  const DataLayout *DL = nullptr;
  AAResults *AA = nullptr;
  const StackSafetyGlobalInfo *SSI = nullptr;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    if (UseStackSafety)
      AU.addRequired<StackSafetyGlobalInfoWrapperPass>();
    if (MergeInit)
      AU.addRequired<AAResultsWrapperPass>();
  }
};

} // end anonymous namespace

char RISCVStackTagging::ID = 0;

INITIALIZE_PASS_BEGIN(RISCVStackTagging, DEBUG_TYPE, "RISCV Stack Tagging",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_DEPENDENCY(StackSafetyGlobalInfoWrapperPass)
INITIALIZE_PASS_END(RISCVStackTagging, DEBUG_TYPE, "RISCV Stack Tagging", false,
                    false)

FunctionPass *llvm::createRISCVStackTaggingPass(bool IsOptNone) {
  return new RISCVStackTagging(IsOptNone);
}

Instruction *RISCVStackTagging::collectInitializers(Instruction *StartInst,
                                                    Value *StartPtr,
                                                    uint64_t Size,
                                                    InitializerBuilder &IB) {
  MemoryLocation AllocaLoc{StartPtr, Size};
  Instruction *LastInst = StartInst;
  BasicBlock::iterator BI(StartInst);

  unsigned Count = 0;
  for (; Count < ClScanLimit && !BI->isTerminator(); ++BI) {
    if (!isa<DbgInfoIntrinsic>(*BI))
      ++Count;

    if (isNoModRef(AA->getModRefInfo(&*BI, AllocaLoc)))
      continue;

    if (!isa<StoreInst>(BI) && !isa<MemSetInst>(BI)) {
      // If the instruction is readnone, ignore it, otherwise bail out.  We
      // don't even allow readonly here because we don't want something like:
      // A[1] = 2; strlen(A); A[2] = 2; -> memcpy(A, ...); strlen(A).
      if (BI->mayWriteToMemory() || BI->mayReadFromMemory())
        break;
      continue;
    }

    if (StoreInst *NextStore = dyn_cast<StoreInst>(BI)) {
      if (!NextStore->isSimple())
        break;

      // Check to see if this store is to a constant offset from the start ptr.
      std::optional<int64_t> Offset =
          NextStore->getPointerOperand()->getPointerOffsetFrom(StartPtr, *DL);
      if (!Offset)
        break;

      if (!IB.addStore(*Offset, NextStore, DL))
        break;
      LastInst = NextStore;
    } else {
      MemSetInst *MSI = cast<MemSetInst>(BI);

      if (MSI->isVolatile() || !isa<ConstantInt>(MSI->getLength()))
        break;

      if (!isa<ConstantInt>(MSI->getValue()))
        break;

      // Check to see if this store is to a constant offset from the start ptr.
      std::optional<int64_t> Offset =
          MSI->getDest()->getPointerOffsetFrom(StartPtr, *DL);
      if (!Offset)
        break;

      if (!IB.addMemSet(*Offset, MSI))
        break;
      LastInst = MSI;
    }
  }
  return LastInst;
}

void RISCVStackTagging::tagAlloca(AllocaInst *AI, Instruction *InsertBefore,
                                  Value *Ptr, uint64_t Size) {
  // auto SetTagZeroFunc =
  // Intrinsic::getDeclaration(F->getParent(), Intrinsic::riscv_settag_zero);
  Function *SetTagZeroFunc = F->getParent()->getFunction("setTagZero");
  if (!SetTagZeroFunc) {
    std::vector<Type *> Args{PointerType::get(F->getParent()->getContext(), 0),
                             Type::getInt64Ty(F->getParent()->getContext())};
    FunctionType *SetTagZeroFuncType = FunctionType::get(
        Type::getVoidTy(F->getParent()->getContext()), Args, false);
    SetTagZeroFunc =
        Function::Create(SetTagZeroFuncType, Function::ExternalLinkage,
                         "setTagZero", F->getParent());
  }

  // auto StgpFunc =
  // Intrinsic::getDeclaration(F->getParent(), Intrinsic::riscv_stgp);
  Function *StgpFunc = F->getParent()->getFunction("stgp");
  if (!StgpFunc) {
    std::vector<Type *> Args{PointerType::get(F->getParent()->getContext(), 0),
                             Type::getInt64Ty(F->getParent()->getContext()),
                             Type::getInt64Ty(F->getParent()->getContext())};
    FunctionType *StgpFuncType = FunctionType::get(
        PointerType::get(F->getParent()->getContext(), 0), Args, false);
    StgpFunc = Function::Create(StgpFuncType, Function::ExternalLinkage, "stgp",
                                F->getParent());
  }

  InitializerBuilder IB(Size, DL, Ptr, SetTagFunc, SetTagZeroFunc, StgpFunc);
  IRBuilder<> IRB(InsertBefore);
  IB.generate(IRB);
}

void RISCVStackTagging::untagAlloca(AllocaInst *AI, Instruction *InsertBefore,
                                    uint64_t Size) {
  IRBuilder<> IRB(InsertBefore);
  IRB.CreateCall(SetTagFunc, {IRB.CreatePointerCast(AI, IRB.getPtrTy()),
                              ConstantInt::get(IRB.getInt64Ty(), Size)});
}

Instruction *RISCVStackTagging::insertBaseTaggedPointer(
    const MapVector<AllocaInst *, memtag::AllocaInfo> &AllocasToInstrument,
    const DominatorTree *DT) {
  BasicBlock *PrologueBB = nullptr;
  // Try sinking IRG as deep as possible to avoid hurting shrink wrap.
  for (auto &I : AllocasToInstrument) {
    const memtag::AllocaInfo &Info = I.second;
    AllocaInst *AI = Info.AI;
    if (!PrologueBB) {
      PrologueBB = AI->getParent();
      continue;
    }
    PrologueBB = DT->findNearestCommonDominator(PrologueBB, AI->getParent());
  }
  assert(PrologueBB);

  IRBuilder<> IRB(&PrologueBB->front());
  // Function *IRG_SP =
  // Intrinsic::getDeclaration(F->getParent(), Intrinsic::riscv_irg_sp);
  Function *IRG_SP = F->getParent()->getFunction("irgSp");
  if (!IRG_SP) {
    std::vector<Type *> Args{Type::getInt64Ty(F->getParent()->getContext())};
    FunctionType *IrgSpFuncType =
        FunctionType::get(IRB.getPtrTy(), Args, false);
    IRG_SP = Function::Create(IrgSpFuncType, Function::ExternalLinkage, "irgSp",
                              F->getParent());
  }

  Instruction *Base =
      IRB.CreateCall(IRG_SP, {Constant::getNullValue(IRB.getInt64Ty())});
  Base->setName("basetag");
  return Base;
}

void RISCVStackTagging::instrumentMalloc(CallInst *CI) {
  for (User *user : CI->users()) {
    // check if its a store
    if (StoreInst *SI = dyn_cast<StoreInst>(user)) {
      // get Address which value is stored to
      Value *address = SI->getPointerOperand();
      // print all users of this address
      for (User *addressUser : address->users()) {
        // Check if its a load
        if (LoadInst *LI = dyn_cast<LoadInst>(addressUser)) {
          // Check if the loaded Value is used in a gep
          if (GetElementPtrInst *GEP =
                  dyn_cast<GetElementPtrInst>(LI->user_back())) {
            if (StructType *STType =
                    dyn_cast<StructType>(GEP->getSourceElementType())) {
              dbgs() << "STType " << *STType << "\n";

              // Check if the size parameter of malloc is a constantint and
              // matches the size of the structtype, otherwise array of structs
              // is allocated which is hard to handle without type information,
              // would need to calculate offsets and insert applyShading
              // manually
              if (ConstantInt *mallocParam =
                      dyn_cast<ConstantInt>(CI->getOperand(0))) {
                if (mallocParam->getZExtValue() !=
                    DL->getTypeAllocSize(STType)) {
                  dbgs() << "malloc size is " << mallocParam->getZExtValue()
                         << " but struct size is "
                         << DL->getTypeAllocSize(STType) << "\n";
                  break;
                }
              }

              IRBuilder<> IRB(CI->getNextNode());
              // Set IO Check Bit

              auto ShadedAllocationBit = ConstantInt::get(IRB.getInt64Ty(), 1);
              Value *MallocLong =
                  IRB.CreateBitOrPointerCast(CI, IRB.getInt64Ty());
              Value *zextShadedAllocation =
                  IRB.CreateZExt(ShadedAllocationBit, IRB.getInt64Ty());

              // Userspace can simply do OR (tag << PointerTagShift);
              Value *ShiftedShadedAllocation =
                  IRB.CreateShl(zextShadedAllocation, 56 + shadeSize + 2);
              Value *clearShadeMask =
                  ConstantInt::get(IRB.getInt64Ty(), 0xF7FFFFFFFFFFFFFFULL);
              Value *NonShadedPtrLong =
                  IRB.CreateAnd(MallocLong, clearShadeMask);
              Value *Replacement =
                  IRB.CreateOr(NonShadedPtrLong, ShiftedShadedAllocation);
              std::string Name = CI->getName().str();
              Replacement->setName(Name + ".base");

              CI->replaceUsesWithIf(Replacement, [MallocLong](Use &U) {
                return U.getUser() != MallocLong;
              });

              applyShading(IRB, STType, CI);

              break;
            }
          }
        }
      }
    }
  }
}

void RISCVStackTagging::instrumentCMP(Instruction *CmpI) {
  IRBuilder<> IRB(CmpI);
  auto op1 = CmpI->getOperand(0);
  auto op2 = CmpI->getOperand(1);
}

bool RISCVStackTagging::instrumentGEP(GetElementPtrInst *GEPI) {
  auto SourceType = GEPI->getSourceElementType();
  auto ResultType = GEPI->getResultElementType();

  IRBuilder<> IRB(GEPI->getNextNode());
  if (SourceType->isStructTy()) {
    dbgs() << "instrumentGEP " << *GEPI << "\n";

    if (ResultType->isStructTy()) {
      // In the case of struct to struct cast, we have to set the IOCheckBit to
      // 0, save and save the OddEvenBit
      dbgs() << "struct to struct \n";
      auto nOperands = GEPI->getNumOperands();

      if (nOperands == 3) {
        auto index = 0;
        if (ConstantInt *CI = dyn_cast<ConstantInt>(GEPI->getOperand(2))) {
          if (CI->getBitWidth() <= 64) {
            index = CI->getSExtValue();
          }
        } else {
          dbgs() << "skipping GEP with non-constant index\n";
          return false;
        }
        StructType *ST = dyn_cast<StructType>(SourceType);

        unsigned flattenedIndex = 0;
        int dummyShade = -1;

        auto bufferVec = getMetadataLayoutFromType(ST, &dummyShade);
        const llvm::StructLayout *Layout = DL->getStructLayout(ST);

        auto offset = Layout->getElementOffset(index);
        auto nContainedTypes = ST->getNumElements();
        unsigned totalOffset = 0;
        dbgs() << "bufferVec: ";
        for (unsigned long i = 0; i < bufferVec.size(); i++) {
          dbgs() << std::get<0>(bufferVec[i]) << " "
                 << std::get<1>(bufferVec[i]) << " ";
        }
        dbgs() << "\n";
        // Count until offsets saved in bufferVec reach offset
        for (auto &pair : bufferVec) {
          if (totalOffset == offset) {
            break;
          }
          totalOffset += std::get<1>(pair);
          flattenedIndex++;
        }
        auto flattenedIndexModSize = flattenedIndex % (1 << shadeSize);
        dbgs() << "flattenedIndex " << flattenedIndex
               << " flattenedIndexModSize " << flattenedIndexModSize << "\n";
        auto oddEven = ConstantInt::get(GEPI->getOperand(2)->getType(),
                                        flattenedIndexModSize);
        Value *GEPILong = IRB.CreateBitOrPointerCast(GEPI, IRB.getInt64Ty());
        Value *zextOddEven = IRB.CreateZExt(oddEven, IRB.getInt64Ty());

        // Userspace can simply do OR (tag << PointerTagShift);
        // Set Odd/Even
        Value *ShiftedOddEven = IRB.CreateShl(zextOddEven, 56);
        Value *XorOddEven = IRB.CreateXor(GEPILong, ShiftedOddEven);
        Value *clearCheckMask =
            ConstantInt::get(IRB.getInt64Ty(), 0xF9FFFFFFFFFFFFFFULL);
        Value *Replacement =
            IRB.CreateAnd(XorOddEven, clearCheckMask);
        std::string Name = GEPI->getName().str();
        Replacement->setName(Name + ".base");

        GEPI->replaceUsesWithIf(Replacement, [GEPILong](Use &U) {
          return U.getUser() != GEPILong;
        });
      }
    } else if ((ResultType->isPointerTy()) || (ResultType->isArrayTy())) {

      dbgs() << "instrumentGEP " << *GEPI << " is pointer or array \n";
      auto nOperands = GEPI->getNumOperands();

      if (nOperands == 3) {
        auto firstIndex = GEPI->getOperand(1);
        bool positiveFirstIndex = true;
        if (ConstantInt *CI = dyn_cast<ConstantInt>(firstIndex)) {
          if (CI->getBitWidth() <= 64) {
            // if (CI->getSExtValue() < 0) {
            if (CI->getSExtValue() != 0) {
              positiveFirstIndex = false;
            };
          }
        }

        // Get Indexed Element from GEPI, then Shade
        auto secondIndex = GEPI->getOperand(2);
        auto index_mod8 =
            IRB.CreateURem(secondIndex, ConstantInt::get(secondIndex->getType(),
                                                         (1 << shadeSize)));

        bool nonZeroIndex = true;
        if (nonZeroIndex && positiveFirstIndex) {
          Value *GEPILong = IRB.CreateBitOrPointerCast(GEPI, IRB.getInt64Ty());
          Value *zextShade = IRB.CreateZExt(index_mod8, IRB.getInt64Ty());

          // Userspace can simply do OR (tag << PointerTagShift);
          Value *ShiftedShade = IRB.CreateShl(zextShade, 56);

          Value *UpdatedGEPI = IRB.CreateXor(GEPILong, ShiftedShade);

          Value *SetCheck = ConstantInt::get(IRB.getInt64Ty(), 1);
          Value *ShiftedSetCheck = IRB.CreateShl(SetCheck, 56 + 2);
          Value *Replacement =
              IRB.CreateOr(UpdatedGEPI, ShiftedSetCheck);

          std::string Name = GEPI->getName().str();
          Replacement->setName(Name + ".shaded");
          dbgs() << "Set shade and io check bit \n";
          GEPI->replaceUsesWithIf(Replacement, [GEPILong](Use &U) {
            return U.getUser() != GEPILong;
          });
        }
      }
    } else {
      dbgs() << "is not struct or pointer or array type, but " << *ResultType
             << "\n";
      // In the case of struct to struct cast, we have to set the IOCheckBit
      // to 0, save and save the OddEvenBit
      auto nOperands = GEPI->getNumOperands();

      if (nOperands == 3) {
        auto index = 0;
        if (ConstantInt *CI = dyn_cast<ConstantInt>(GEPI->getOperand(2))) {
          if (CI->getBitWidth() <= 64) {
            index = CI->getSExtValue();
          }
        } else {
          dbgs() << "skipping GEP with non-constant index\n";
          return false;
        }
        Value *GEPILong = IRB.CreateBitOrPointerCast(GEPI, IRB.getInt64Ty());

        Value *clearCheckAndShadeMask =
            ConstantInt::get(IRB.getInt64Ty(), 0xFBFFFFFFFFFFFFFFULL);
        Value *NonShadedPtrLong =
            IRB.CreateAnd(GEPILong, clearCheckAndShadeMask);
        std::string Name = GEPI->getName().str();
        NonShadedPtrLong->setName(Name + ".base");

        GEPI->replaceUsesWithIf(NonShadedPtrLong, [GEPILong](Use &U) {
          return U.getUser() != GEPILong;
        });
      }
    }
  }
  return false;
}

std::vector<std::tuple<int, int>>
RISCVStackTagging::getMetadataLayoutFromType(StructType *ST, int *shade) {

  // dbgs() << "getMetadataLayoutFromType prevShade " << *shade
  //        << " called with " << *ST << "\n";
  /*
    auto it = structTypeToBufferMap.find(ST);
    if (it != structTypeToBufferMap.end()) {
      // If the buffer is in the map, return it
      return it->second;
    } else {*/

  std::vector<std::tuple<int, int>> bufferVec;

  const llvm::StructLayout *Layout = DL->getStructLayout(ST);
  auto nContainedTypes = ST->getNumElements();

  for (unsigned int i = 0; i < nContainedTypes; i++) {

    unsigned int size;
    if (i == nContainedTypes -
                 1) { // last field's size is total struct size minus its offset
      size = Layout->getSizeInBytes() - Layout->getElementOffset(i);
    } else { // size is difference between next field's offset and this
             // field's
      // offset
      size = Layout->getElementOffset(i + 1) - Layout->getElementOffset(i);
    }

    assert((size > 0) && "zero_size");

    auto elementType = ST->getContainedType(i);
    if (StructType *nestedST = dyn_cast<StructType>(elementType)) {
      auto subBufferVec = getMetadataLayoutFromType(nestedST, shade);

      bufferVec.insert(bufferVec.end(), subBufferVec.begin(),
                       subBufferVec.end());
      uint64_t subBufferVecTotalSize = 0;
      for (unsigned long j = 0; j < subBufferVec.size(); j++) {
        subBufferVecTotalSize += std::get<1>(subBufferVec[j]);
      }
      if (subBufferVecTotalSize < size) {
        // Pad according to size
        unsigned int difference = size - subBufferVecTotalSize;

        bufferVec.push_back(std::make_tuple(0, difference));
      }
    } else {
      *shade += 1;
      *shade = *shade % (1 << shadeSize);
      if (elementType->isArrayTy()) {
        auto aType = dyn_cast<ArrayType>(elementType);
        auto nType = aType->getElementType();
        if (nType->isStructTy()) {
          bufferVec.push_back(std::make_tuple(0, size));
        } else {
          bufferVec.push_back(std::make_tuple(*shade, size));
        }
      } else if (elementType->isVectorTy()) {
        auto vType = dyn_cast<VectorType>(elementType);
        bufferVec.push_back(std::make_tuple(*shade, size));
        if (vType->getElementType()->isStructTy()) {
          assert(0 && "Vector with StructTypes not yet handeled");
        }
      } else {
        bufferVec.push_back(std::make_tuple(*shade, size));
      }
    }
    //}

    // Store the buffer in the map
  }
  return bufferVec;
}

bool RISCVStackTagging::isAllocationFunction(CallBase *CB) {
  auto allocation_names = {
      "malloc", "calloc",
      "realloc"};

  if (CB->getCalledFunction()) {
    if (CB->getCalledFunction()->hasName()) {
      if (std::find(allocation_names.begin(), allocation_names.end(),
                    CB->getCalledFunction()->getName()) !=
          allocation_names.end()) {
        return true;
      }
    }
  }
  return false;
}

bool RISCVStackTagging::containsUnion(StructType *ST) {

  if (!ST->isLiteral()) {
    if (ST->getName().startswith("union")) {
      return true;
    }
  }

  for (unsigned int i = 0; i < ST->getNumElements(); ++i) {
    llvm::Type *fieldType = ST->getElementType(i);

    if (fieldType->isStructTy()) {
      if (containsUnion(llvm::cast<llvm::StructType>(fieldType))) {
        return true;
      }
    }
  }

  return false;
}

void RISCVStackTagging::applyShading(IRBuilder<> &IRB, StructType *type,
                                     Value *AI) {
#ifdef DISABLE_SHADING_ALLOCATIONS
  return;
#endif
  // dbgs() << "applyShading AI " << *AI << " with type " << *type << "\n";
  const llvm::StructLayout *Layout = DL->getStructLayout(type);
  // auto nContainedTypes = type->getNumContainedTypes();

  if (containsUnion(type)) {
    return;
  }
  int currentShade = -1;
  std::vector<std::tuple<int, int>> shadeVector =
      getMetadataLayoutFromType(type, &currentShade);
  dbgs() << "shadeVector: for type " << *type << " ";
  for (unsigned long i = 0; i < shadeVector.size(); i++) {
    dbgs() << std::get<0>(shadeVector[i]) << " " << std::get<1>(shadeVector[i])
           << " ";
  }
  dbgs() << "\n";
  uint64_t sum = 0;
  for (unsigned long i = 0; i < shadeVector.size(); i++) {
    sum += std::get<1>(shadeVector[i]);
  }
  uint64_t structSize = DL->getTypeAllocSize(type);

  assert((sum == structSize) && "Size of shades different to size of type");

  unsigned int offset = 0;
  for (unsigned i = 0; i < shadeVector.size(); ++i) {
    Value *TargetAddress = IRB.CreateInBoundsGEP(
        IRB.getInt8Ty(), AI, ConstantInt::get(IRB.getInt32Ty(), offset));
    Value *JustShade = IRB.CreateTrunc(
        ConstantInt::get(IRB.getInt8Ty(), std::get<0>(shadeVector[i])),
        IRB.getInt64Ty());
    IRB.CreateCall(ShadeFunc, {TargetAddress, JustShade,
                               ConstantInt::get(IRB.getInt64Ty(),
                                                std::get<1>(shadeVector[i]))});
    offset += std::get<1>(shadeVector[i]);
  }
}

// FIXME: check for MTE extension
bool RISCVStackTagging::runOnFunction(Function &Fn) {

  uint64_t stackObjects = 0;
  uint64_t alignsSmaller16 = 0;
  uint64_t alignsSmaller32 = 0;
  uint64_t alignsSmaller64 = 0;
  uint64_t alignsBigger64 = 0;
  uint64_t BytesInsertedfor16 = 0;
  uint64_t BytesInsertedfor32 = 0;
  uint64_t BytesInsertedfor64 = 0;
  dbgs() << "UseStackSafety: " << UseStackSafety << "\n";

  // print function before instrumentation
  if (getenv("DPRINTINST")) {
    dbgs() << Fn << "\n";
  }

  // return true;

  LLVMContext &Ctx = Fn.getContext();
  Module *M = Fn.getParent();

  // if (!Fn.hasFnAttribute(Attribute::SanitizeMemTag))
  // return false;

  if (UseStackSafety)
    SSI = &getAnalysis<StackSafetyGlobalInfoWrapperPass>().getResult();
  F = &Fn;
  DL = &Fn.getParent()->getDataLayout();
  if (MergeInit)
    AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();

  memtag::StackInfoBuilder SIB(SSI);
  for (Instruction &I : instructions(F))
    SIB.visit(I);
  memtag::StackInfo &SInfo = SIB.get();

  std::unique_ptr<DominatorTree> DeleteDT;
  DominatorTree *DT = nullptr;
  if (auto *P = getAnalysisIfAvailable<DominatorTreeWrapperPass>())
    DT = &P->getDomTree();

  if (DT == nullptr) {
    DeleteDT = std::make_unique<DominatorTree>(*F);
    DT = DeleteDT.get();
  }

  std::unique_ptr<PostDominatorTree> DeletePDT;
  PostDominatorTree *PDT = nullptr;
  if (auto *P = getAnalysisIfAvailable<PostDominatorTreeWrapperPass>())
    PDT = &P->getPostDomTree();

  if (PDT == nullptr) {
    DeletePDT = std::make_unique<PostDominatorTree>(*F);
    PDT = DeletePDT.get();
  }

  std::unique_ptr<LoopInfo> DeleteLI;
  LoopInfo *LI = nullptr;
  if (auto *LIWP = getAnalysisIfAvailable<LoopInfoWrapperPass>()) {
    LI = &LIWP->getLoopInfo();
  } else {
    DeleteLI = std::make_unique<LoopInfo>(*DT);
    LI = DeleteLI.get();
  }

  // SetTagFunc =
  // Intrinsic::getDeclaration(F->getParent(), Intrinsic::riscv_settag);
  SetTagFunc = M->getFunction("setTag");

  if (!SetTagFunc) {
    std::vector<Type *> Args{PointerType::get(Ctx, 0), Type::getInt64Ty(Ctx)};
    FunctionType *SetTagFuncType =
        FunctionType::get(Type::getVoidTy(Ctx), Args, false);
    SetTagFunc = Function::Create(SetTagFuncType, Function::ExternalLinkage,
                                  "setTag", M);
  }

  ShadeFunc = M->getFunction("setShade");
  if (!ShadeFunc) {
    std::vector<Type *> Args{PointerType::get(Ctx, 0), Type::getInt8Ty(Ctx),
                             Type::getInt64Ty(Ctx)};
    FunctionType *ShadeFuncType =
        FunctionType::get(Type::getVoidTy(Ctx), Args, false);
    ShadeFunc = Function::Create(ShadeFuncType, Function::ExternalLinkage,
                                 "setShade", M);
  }

  if (getenv("DSHADE")) {

    SmallVector<GetElementPtrInst *, 4> GEPsToInstrument;
    SmallVector<CallInst *, 4> MallocsToInstrument;
    SmallVector<CallInst *, 4> CmpsToInstrument;

    for (Instruction &I : instructions(F)) {
      if (GetElementPtrInst *GEPI = dyn_cast<GetElementPtrInst>(&I)) {
        GEPsToInstrument.push_back(GEPI);
      }

      if (CallInst *CI = dyn_cast<CallInst>(&I)) {
        if (isAllocationFunction(CI)) {
          MallocsToInstrument.push_back(CI);
        }
      }

    }

    dbgs() << "MallocsToInstruments: " << MallocsToInstrument.size() << "\n";

    for (auto &CI : MallocsToInstrument) {
      instrumentMalloc(CI);
    }

    for (auto &GEPI : GEPsToInstrument) {
      instrumentGEP(GEPI);
    }

    for (auto &CI : CmpsToInstrument) {
      // instrumentCmp(CI);
    }

    if (getenv("DPRINTINST")) {
      dbgs() << Fn << "\n";
    }
  }

  // Check if environmental Variable DSTACKTAG is set
  if (getenv("DSTACKTAG")) {
    dbgs() << "Stack tagging\n";

    if (SInfo.AllocasToInstrument.empty())
      return false;

    Instruction *Base = insertBaseTaggedPointer(SInfo.AllocasToInstrument, DT);

    int NextTag = 0;
    for (auto &I : SInfo.AllocasToInstrument) {
      memtag::AllocaInfo &Info = I.second;
      assert(Info.AI && SIB.isInterestingAlloca(*Info.AI));
      const Align OldAlignment = Info.AI->getAlign();
      stackObjects++;
      if (OldAlignment < 16) {
        alignsSmaller16++;
        BytesInsertedfor16 += 16 - OldAlignment.value();
      }
      if (OldAlignment < 32) {
        alignsSmaller32++;
        BytesInsertedfor32 += 32 - OldAlignment.value();
      }
      if (OldAlignment < 64) {
        alignsSmaller64++;
        BytesInsertedfor64 += 64 - OldAlignment.value();
      }

      if (OldAlignment > 64) {
        alignsBigger64++;
      }

      memtag::alignAndPadAlloca(Info, kTagGranuleSize);
      AllocaInst *AI = Info.AI;
      int Tag = NextTag;
      NextTag = (NextTag + 1) % 16;
      // Replace alloca with tagp(alloca).
      IRBuilder<> IRB(Info.AI->getNextNode());
      // Function *TagP = Intrinsic::getDeclaration(
      // F->getParent(), Intrinsic::riscv_tagp, {Info.AI->getType()});
      Function *TagP = M->getFunction("tagP");
      if (!TagP) {
        std::vector<Type *> Args{Info.AI->getType(), IRB.getPtrTy(),
                                 Type::getInt64Ty(Ctx)};
        FunctionType *TagPFuncType =
            FunctionType::get(IRB.getPtrTy(), Args, false);
        TagP = Function::Create(TagPFuncType, Function::ExternalLinkage, "tagP",
                                M);
      }
      Instruction *TagPCall = IRB.CreateCall(
          TagP, {AI, Base, ConstantInt::get(IRB.getInt64Ty(), Tag)});

      if (Info.AI->hasName())
        TagPCall->setName(Info.AI->getName() + ".tag");
      // Does not replace metadata, so we don't have to handle
      // DbgVariableRecords.
      Info.AI->replaceUsesWithIf(TagPCall, [&](const Use &U) {
        return !memtag::isLifetimeIntrinsic(U.getUser());
      });
      TagPCall->setOperand(0, Info.AI);

      // Calls to functions that may return twice (e.g. setjmp) confuse the
      // postdominator analysis, and will leave us to keep memory tagged after
      // function return. Work around this by always untagging at every return
      // statement if return_twice functions are called.
      bool StandardLifetime =
          !SInfo.CallsReturnTwice && SInfo.UnrecognizedLifetimes.empty() &&
          memtag::isStandardLifetime(Info.LifetimeStart, Info.LifetimeEnd, DT,
                                     LI, ClMaxLifetimes);
      if (StandardLifetime) {
        IntrinsicInst *Start = Info.LifetimeStart[0];
        uint64_t Size =
            cast<ConstantInt>(Start->getArgOperand(0))->getZExtValue();
        Size = alignTo(Size, kTagGranuleSize);
        tagAlloca(AI, Start->getNextNode(), TagPCall, Size);

        auto TagEnd = [&](Instruction *Node) { untagAlloca(AI, Node, Size); };
        if (!DT || !PDT ||
            !memtag::forAllReachableExits(*DT, *PDT, *LI, Start,
                                          Info.LifetimeEnd, SInfo.RetVec,
                                          TagEnd)) {
          for (auto *End : Info.LifetimeEnd)
            End->eraseFromParent();
        }
      } else {
        uint64_t Size = *Info.AI->getAllocationSize(*DL);
        Value *Ptr = IRB.CreatePointerCast(TagPCall, IRB.getPtrTy());
        tagAlloca(AI, &*IRB.GetInsertPoint(), Ptr, Size);
        for (auto *RI : SInfo.RetVec) {
          untagAlloca(AI, RI, Size);
        }
        // We may have inserted tag/untag outside of any lifetime interval.
        // Remove all lifetime intrinsics for this alloca.
        for (auto *II : Info.LifetimeStart)
          II->eraseFromParent();
        for (auto *II : Info.LifetimeEnd)
          II->eraseFromParent();
      }
    }

    // If we have instrumented at least one alloca, all unrecognized lifetime
    // intrinsics have to go.
    for (auto *I : SInfo.UnrecognizedLifetimes)
      I->eraseFromParent();
  }

  return true;
}
