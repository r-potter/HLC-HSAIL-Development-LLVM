#include "BRIGAsmPrinter.h"
#include "BRIGDwarfDebug.h"
#include "MCTargetDesc/BRIGDwarfStreamer.h"
#include "HSAILKernelManager.h"
#include "HSAILMachineFunctionInfo.h"
#include "HSAILOpaqueTypes.h"
#include "HSAILTargetMachine.h"
#include "HSAILLLVMVersion.h"
#include "HSAILUtilityFunctions.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/VariadicFunction.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCObjectStreamer.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/Mangler.h"
#include "../lib/CodeGen/AsmPrinter/DwarfDebug.h"
#include <sstream>
#include <iostream>
#include <fstream>

#include "LibHSAILAdapters.h"
#include "libHSAIL/HSAILConvertors.h"
#include "libHSAIL/HSAILValidator.h"
#include "libHSAIL/HSAILBrigObjectFile.h"
#include "libHSAIL/HSAILDisassembler.h"
#include "libHSAIL/HSAILUtilities.h"
#include "libHSAIL/HSAILDump.h"
#include "libHSAIL/HSAILFloats.h"

#include <memory>

using namespace llvm;
using std::string;

#include "HSAILGenMnemonicMapper.inc"
#define GET_LLVM_INTRINSIC_FOR_GCC_BUILTIN
#include "HSAILGenIntrinsics.inc"
#undef GET_LLVM_INTRINSIC_FOR_GCC_BUILTIN

namespace llvm {
  extern bool EnableExperimentalFeatures;
  extern bool EnableUniformOperations;
  inline std::ostream& operator<<(std::ostream& s, StringRef arg) {
    s.write(arg.data(), arg.size()); return s;
  }
}

static cl::opt<std::string> DebugInfoFilename("odebug",
  cl::desc("Debug Info filename"), cl::value_desc("filename"), cl::init(""));

static cl::opt<std::string> DumpOnFailFilename("dumpOnFail",
  cl::desc("Filename for the BRIG container dump if validation failed"),
  cl::value_desc("filename"), cl::init(""));

static cl::opt<bool>DisableValidator("disable-validator",
  cl::desc("Disable validation of the BRIG container"),
  cl::init(false), cl::Hidden);

static cl::opt<bool> PrintBeforeBRIG("print-before-brig",
  llvm::cl::desc("Print LLVM IR just before emitting BRIG"), cl::Hidden);


unsigned MAX(unsigned a, unsigned b) {
  return a > b ? a: b;
}

unsigned MAX(unsigned a, unsigned b, unsigned c) {
  return (a > b) ? ( (a > c ) ? a : c ) : (b > c) ? b : c;
}

namespace HSAIL_ATOMIC_OPS {
  enum {
    OPCODE_INDEX = 0,
    SEGMENT_INDEX = 1,
    ORDER_INDEX = 2,
    SCOPE_INDEX = 3,
    TYPE_INDEX = 4
  };
}

namespace HSAIL_MEMFENCE {
  enum {
    MEMORY_ORDER_INDEX = 0,
    GLOBAL_SCOPE_INDEX = 1,
    GROUP_SCOPE_INDEX = 2,
    IMAGE_SCOPE_INDEX = 3
  };
}

static int getAtomicParameter(const llvm::MachineInstr *MI, unsigned index) {
  assert(HSAIL::isAtomicOp(MI));

  int offset = 0;

  //Ret versions have destination operand at 0 index, so offset parameters index by 1
  if (HSAIL::isRetAtomicOp(MI))
    offset = 1;

  llvm::MachineOperand opc = MI->getOperand(index + offset);

  assert(opc.isImm());
  return opc.getImm();
}

static Brig::BrigAtomicOperation getAtomicOpcode(const llvm::MachineInstr *MI) {
  int val = getAtomicParameter(MI, HSAIL_ATOMIC_OPS::OPCODE_INDEX);
  assert(val >= Brig::BRIG_ATOMIC_ADD && val <= Brig::BRIG_ATOMIC_XOR);
  return (Brig::BrigAtomicOperation)val;
}

static Brig::BrigSegment getAtomicSegment(const llvm::MachineInstr *MI) {
  int val = getAtomicParameter(MI, HSAIL_ATOMIC_OPS::SEGMENT_INDEX);
  assert(val > 0 && val < Brig::BRIG_SEGMENT_EXTSPACE0);
  return (Brig::BrigSegment)val;
}

static Brig::BrigMemoryOrder getAtomicOrder(const llvm::MachineInstr *MI) {
  int val = getAtomicParameter(MI, HSAIL_ATOMIC_OPS::ORDER_INDEX);
  assert(val > 0 && val <= Brig::BRIG_MEMORY_ORDER_SC_ACQUIRE_RELEASE);
  return (Brig::BrigMemoryOrder)val;
}

static Brig::BrigMemoryScope getAtomicScope(const llvm::MachineInstr *MI) {
  int val = getAtomicParameter(MI, HSAIL_ATOMIC_OPS::SCOPE_INDEX);
  assert(val > 0 && val <= Brig::BRIG_MEMORY_SCOPE_SYSTEM);
  return (Brig::BrigMemoryScope)val;
}

static Brig::BrigTypeX getAtomicType(const llvm::MachineInstr *MI) {
  int val = getAtomicParameter(MI, HSAIL_ATOMIC_OPS::TYPE_INDEX);
  switch (val) {
  case Brig::BRIG_TYPE_B32:
  case Brig::BRIG_TYPE_S32:
  case Brig::BRIG_TYPE_U32:
  case Brig::BRIG_TYPE_B64:
  case Brig::BRIG_TYPE_S64:
  case Brig::BRIG_TYPE_U64:
    break;
    default:
    llvm_unreachable("Unknown BrigType");
  }

  return (Brig::BrigTypeX)val;
}

class LLVM_LIBRARY_VISIBILITY StoreInitializer {
  Brig::BrigType16_t       m_type;
  BRIGAsmPrinter&          m_asmPrinter;
  unsigned                 m_reqNumZeroes;
  HSAIL_ASM::ArbitraryData m_data;

  template <Brig::BrigTypeX BrigTypeId>
  void pushValue(typename HSAIL_ASM::BrigType<BrigTypeId>::CType value);

  template <Brig::BrigTypeX BrigTypeId>
  void pushValueImpl(typename HSAIL_ASM::BrigType<BrigTypeId>::CType value);

  void initVarWithAddress(const Value *V, const std::string Var,
                          const APInt& Offset);

public:
  StoreInitializer(Brig::BrigType16_t type, BRIGAsmPrinter& asmPrinter)
      : m_type(type)
      , m_asmPrinter(asmPrinter)
      , m_reqNumZeroes(0) {}

  void append(const Constant* CV, const std::string Var);

  HSAIL_ASM::SRef toSRef() const { return m_data.toSRef(); }

  size_t elementCount() const {
    return m_data.numBytes() / HSAIL_ASM::getBrigTypeNumBytes(m_type);
  }

  size_t dataSizeInBytes() const {
    return m_data.numBytes();
  }

};

template <Brig::BrigTypeX BrigTypeId> void StoreInitializer::pushValue(
  typename HSAIL_ASM::BrigType<BrigTypeId>::CType value) {

  using namespace Brig;
  // Need to check whether BrigTypeId matches initializer type
  if (m_type == HSAIL_ASM::BrigType<BrigTypeId>::asBitType::value) {
    pushValueImpl<BrigTypeId>(value);
    return;
  }

  assert(m_type == BRIG_TYPE_B8); // struct case
  const uint8_t (&src)[ sizeof value ] =
    *reinterpret_cast<const uint8_t (*)[ sizeof value ]>(&value);

  for (unsigned int i=0; i<sizeof value; ++i) {
    pushValueImpl<BRIG_TYPE_U8>(src[i]);
  }
}

template <Brig::BrigTypeX BrigTypeId> void StoreInitializer::pushValueImpl(
  typename HSAIL_ASM::BrigType<BrigTypeId>::CType value) {

  assert(m_type == HSAIL_ASM::BrigType<BrigTypeId>::asBitType::value);
  typedef typename HSAIL_ASM::BrigType<BrigTypeId>::CType CType;
  if (m_reqNumZeroes > 0) {
    for (unsigned i = m_reqNumZeroes; i > 0; --i) {
      m_data.push_back(CType());
    }
    m_reqNumZeroes = 0;
  }
  m_data.push_back(value);
}

void StoreInitializer::initVarWithAddress(const Value *V, const std::string Var,
                                          const APInt& Offset) {
  assert(V->hasName());
  std::stringstream initstr;
  initstr << "initvarwithaddress:" << Var << ":" << dataSizeInBytes() <<
    ":" << HSAIL_ASM::getBrigTypeNumBytes(m_type) << ":" <<
    BRIGAsmPrinter::getSymbolPrefix(*dyn_cast<GlobalVariable>(V)) <<
    V->getName().data() << ":" << Offset.toString(10, false);
  HSAIL_ASM::DirectivePragma pgm = m_asmPrinter.brigantine.append<HSAIL_ASM::DirectivePragma>();
  HSAIL_ASM::ItemList opnds;
  opnds.push_back(m_asmPrinter.brigantine.createOperandString(initstr.str()));
  pgm.operands() = opnds;
  (m_asmPrinter.getSubtarget().is64Bit()) ?
    pushValue<Brig::BRIG_TYPE_B64>(0) : pushValue<Brig::BRIG_TYPE_B32>(0);
}

void StoreInitializer::append(const Constant *CV, const std::string Var) {
  using namespace Brig;

  switch(CV->getValueID() ) {
  case Value::ConstantArrayVal: { // recursive type
    const ConstantArray *CA =  cast<ConstantArray>(CV);
    for (unsigned i = 0, e = CA->getNumOperands(); i < e; ++i) {
      append(cast<Constant>(CA->getOperand(i)), Var);
    }
    break;
  }
  case Value::ConstantDataArrayVal: {
    const ConstantDataArray *CVE = cast<ConstantDataArray>(CV);
    for (unsigned i = 0, e = CVE->getNumElements(); i < e; ++i) {
      append(cast<Constant>(CVE->getElementAsConstant(i)), Var);
    }
    break;
  }
  case Value::ConstantStructVal: { // recursive type
    const ConstantStruct * s = cast<ConstantStruct>(CV);
    const StructLayout *layout = m_asmPrinter.getDataLayout()
                                 .getStructLayout(s->getType());
    uint64_t const initStartOffset = m_data.numBytes();
    for (unsigned i = 0, e = s->getNumOperands(); i < e; ++i) {
      append(cast<Constant>(s->getOperand(i)), Var);
      // Match structure layout by padding with zeroes
      uint64_t const nextElemOffset = (i+1) < e ?
                                      layout->getElementOffset(i+1) :
                                      layout->getSizeInBytes();
      uint64_t const structInitSize = m_data.numBytes() - initStartOffset;
      assert(nextElemOffset >= structInitSize);
      m_reqNumZeroes = nextElemOffset - structInitSize;
      if (m_reqNumZeroes) {
        m_reqNumZeroes--;
        pushValue<BRIG_TYPE_U8>(0);
      }
    }
    break;
  }
  case Value::ConstantVectorVal: { // almost leaf type
    const ConstantVector *CVE = cast<ConstantVector>(CV);
    for (unsigned i = 0, e = CVE->getType()->getNumElements(); i < e; ++i) {
      append(cast<Constant>(CVE->getOperand(i)), Var);
    }
    break;
  }
  case Value::ConstantDataVectorVal: {
    const ConstantDataVector *CVE = cast<ConstantDataVector>(CV);
    for (unsigned i = 0, e = CVE->getNumElements(); i < e; ++i) {
      append(cast<Constant>(CVE->getElementAsConstant(i)), Var);
    }
    break;
  }
  case Value::ConstantIntVal: {
    const ConstantInt *CI = cast<ConstantInt>(CV);
    if (CI->getType()->isIntegerTy(1)) {
      pushValue<BRIG_TYPE_B1>(CI->getZExtValue() ? 1 : 0);
    } else {
      switch(CI->getBitWidth()) {
      case 8:  pushValue<BRIG_TYPE_U8> (*CI->getValue().getRawData()); break;
      case 16: pushValue<BRIG_TYPE_U16>(*CI->getValue().getRawData()); break;
      case 32: pushValue<BRIG_TYPE_U32>(*CI->getValue().getRawData()); break;
      case 64: pushValue<BRIG_TYPE_U64>(*CI->getValue().getRawData()); break;
      }
    }
    break;
  }
  case Value::ConstantFPVal: {
    const ConstantFP *CFP = cast<ConstantFP>(CV);
    if (CFP->getType()->isFloatTy()) {
      pushValue<BRIG_TYPE_F32>(HSAIL_ASM::f32_t::fromRawBits(
        *CFP->getValueAPF().bitcastToAPInt().getRawData()));
    } else if (CFP->getType()->isDoubleTy()) {
      pushValue<BRIG_TYPE_F64>(HSAIL_ASM::f64_t::fromRawBits(
        *CFP->getValueAPF().bitcastToAPInt().getRawData()));
    }
    break;
  }
  case Value::ConstantPointerNullVal:
    (m_asmPrinter.getSubtarget().is64Bit()) ?
      pushValue<BRIG_TYPE_B64>(0) :
      pushValue<BRIG_TYPE_B32>(0);
    break;

  case Value::ConstantAggregateZeroVal:
    m_reqNumZeroes += HSAIL::getNumElementsInHSAILType(CV->getType(),
                                                  m_asmPrinter.getDataLayout());
    break;

  case Value::ConstantExprVal: {
    const ConstantExpr* CE;
    CE = dyn_cast<ConstantExpr>(CV);
    assert(CE != NULL);
    if (CE->isGEPWithNoNotionalOverIndexing()) {
      const GEPOperator* GO = dyn_cast<GEPOperator>(CE);
      if (GO) {
        const Value* Ptr = GO->getPointerOperand()->stripPointerCasts();
        assert(Ptr != NULL && Ptr->hasName());
        APInt Offset(m_asmPrinter.getSubtarget().is64Bit() ? 64 : 32, 0);
        if (!GO->accumulateConstantOffset(m_asmPrinter.getDataLayout(), Offset))
          llvm_unreachable("Cannot calculate initializer offset");
        initVarWithAddress(Ptr, Var, Offset);
      } else {
        llvm_unreachable("Unhandled ConstantExpr initializer instruction");
      }
    } else if (CE->getOpcode() == Instruction::BitCast) {
      append(cast<Constant>(CE->getOperand(0)), Var);
    } else {
      llvm_unreachable("Unhandled ConstantExpr initializer type");
    }
    break;
  }

  case Value::GlobalVariableVal: {
    const Value* V = CV->stripPointerCasts();
    assert(V->hasName());
    initVarWithAddress(V, Var,
      APInt(m_asmPrinter.getSubtarget().is64Bit() ? 64 : 32, 0));
    break;
  }

  default: assert(!"Unhandled initializer");
  }
}

void BRIGAsmPrinter::BrigEmitGlobalInit(HSAIL_ASM::DirectiveVariable globalVar,
                                        Constant *CV) {
  StoreInitializer store(HSAIL_ASM::convType2BitType(globalVar.type()), *this);
  store.append(CV, globalVar.name().str());
  size_t typeBytes = HSAIL_ASM::getBrigTypeNumBytes(globalVar.type());

  HSAIL_ASM::SRef init;
  char zeroes[32];
  if (store.elementCount() > 0) {
    init = store.toSRef();
  } else {
    assert(typeBytes <= sizeof zeroes);
    memset(zeroes, 0, typeBytes);
    init = HSAIL_ASM::SRef(zeroes, zeroes + typeBytes);
  }

  if (globalVar.modifier().isArray()) {
    assert(globalVar.dim() * typeBytes  >= init.length());
  } else {
    assert(globalVar.dim() == 0 && typeBytes == init.length());
  }

  globalVar.init() = brigantine.createOperandData(init);
}

BRIGAsmPrinter::BRIGAsmPrinter(HSAIL_ASM_PRINTER_ARGUMENTS)
  : AsmPrinter(ASM_PRINTER_ARGUMENTS),
    brigantine(bc) {

  Subtarget = &TM.getSubtarget<HSAILSubtarget>();
  FuncArgsStr = "";
  FuncRetValStr = "";
  retValCounter = 0;
  paramCounter = 0;
  reg1Counter = 0;
  reg32Counter = 0;
  reg64Counter = 0;
  mTM = reinterpret_cast<HSAILTargetMachine*>(&TM);
  mMeta = new HSAILKernelManager(mTM);
  mMFI = NULL;
  mBuffer = 0;
  m_bIsKernel = false;
  privateStackBRIGOffset = 0;
  spillStackBRIGOffset = 0;

  mDwarfFileStream = 0;

  // Obtain DWARF stream
  BRIGDwarfStreamer* dwarfstreamer = dyn_cast<BRIGDwarfStreamer>(&OutStreamer);
  assert(dwarfstreamer && "BRIG lowering doesn't work with this kind of streamer");
  mDwarfStream = dwarfstreamer->getDwarfStream();
  // Obtain stream for streaming BRIG that came from LLC
  mBrigStream = mDwarfStream->getOtherStream();
  // Disconnect DWARF stream from BRIG stream
  mDwarfStream->releaseStream();
  if (DebugInfoFilename.size() > 0) {
    std::error_code err;
    mDwarfFileStream = new raw_fd_ostream(DebugInfoFilename.c_str(), err,
                                          sys::fs::F_Text);
    mDwarfStream->setOtherStream(mDwarfFileStream);
  }
}

BRIGAsmPrinter::~BRIGAsmPrinter() {
  delete mMeta;
  delete mDwarfStream;
  delete mDwarfFileStream;
}

Brig::BrigSegment8_t BRIGAsmPrinter::getHSAILSegment(unsigned AddressSpace)
                                                     const {
  switch (AddressSpace) {
  case HSAILAS::PRIVATE_ADDRESS:  return Brig::BRIG_SEGMENT_PRIVATE;
  case HSAILAS::GLOBAL_ADDRESS:   return Brig::BRIG_SEGMENT_GLOBAL;
  case HSAILAS::CONSTANT_ADDRESS: return Brig::BRIG_SEGMENT_READONLY;
  case HSAILAS::GROUP_ADDRESS:    return Brig::BRIG_SEGMENT_GROUP;
  case HSAILAS::FLAT_ADDRESS:     return Brig::BRIG_SEGMENT_FLAT;
  case HSAILAS::REGION_ADDRESS:   return Brig::BRIG_SEGMENT_EXTSPACE0;
  case HSAILAS::KERNARG_ADDRESS:  return Brig::BRIG_SEGMENT_KERNARG;
  case HSAILAS::ARG_ADDRESS:      return Brig::BRIG_SEGMENT_ARG;
  case HSAILAS::SPILL_ADDRESS:    return Brig::BRIG_SEGMENT_SPILL;
  }
  llvm_unreachable("Unexpected BRIG address space value");
}

Brig::BrigSegment8_t BRIGAsmPrinter::getHSAILSegment(const GlobalVariable* gv)
                                                     const {
  return getHSAILSegment(gv->getType()->getAddressSpace());
}

bool BRIGAsmPrinter::canInitHSAILAddressSpace(const GlobalVariable* gv) const {
  bool canInit;
  switch (gv->getType()->getAddressSpace()) {
  case HSAILAS::GLOBAL_ADDRESS:
  case HSAILAS::CONSTANT_ADDRESS:
    canInit = true;
    break;
  default:
    canInit = false;
    break;
  }
  return canInit;
}

/// EmitGlobalVariable - Emit the specified global variable to the .s file.
void BRIGAsmPrinter::EmitGlobalVariable(const GlobalVariable *GV)
{
  if (HSAIL::isIgnoredGV(GV))
    return;

  std::stringstream ss;
  ss << getSymbolPrefix(*GV) << GV->getName();

  string nameString = ss.str();

  const DataLayout& DL = getDataLayout();
  Type *ty = GV->getType()->getElementType();
  if (ty->isIntegerTy(1)) {
   // HSAIL do not support boolean load and stores and all boolean variables
   // should have been promoted to int8. However, due to llvm optimization
   // such as shrink to boolean optimization we will still get i1 types.
   // The loads and stores of this global variables are handled in
   // selection lowering by extending to int8 types.The type of global
   // variable will be changed to i8 here.
    ty = Type::getInt8Ty(GV->getContext());
  }
  const bool isArray = HSAIL::HSAILrequiresArray(ty);

  // TODO_HSA: pending BRIG_LINKAGE_STATIC implementation in the Finalizer
  HSAIL_ASM::DirectiveVariable globalVar =
    brigantine.addVariable(nameString, getHSAILSegment(GV),
    HSAIL::HSAILgetBrigType(ty, Subtarget->is64Bit()));

  globalVar.linkage() = GV->isExternalLinkage(GV->getLinkage()) ?
      Brig::BRIG_LINKAGE_PROGRAM : ( GV->isInternalLinkage(GV->getLinkage()) ?
     Brig::BRIG_LINKAGE_MODULE : Brig::BRIG_LINKAGE_NONE );
  globalVar.allocation() = Brig::BRIG_ALLOCATION_AGENT;
  globalVar.modifier().isDefinition() = 1;

  globalVar.modifier().isArray() = isArray;
  globalVar.dim() = isArray ? HSAIL::getNumElementsInHSAILType(ty, DL) : 0;
  // Align arrays at least by 4 bytes
  unsigned align_value = std::max((globalVar.dim() > 1) ? 4U : 0U,
    std::max(GV->getAlignment(),
             HSAIL::HSAILgetAlignTypeQualifier(GV->getType()->getElementType(), DL, true)));
  globalVar.align() = getBrigAlignment(align_value);

  globalVariableOffsets[GV] = globalVar.brigOffset();
#if 0
  printf("GV %s[%p] is at offset %lu\n", nameString.c_str(), (const void*)(GV),
         (unsigned long)(globalVar.brigOffset()));
#endif

  // TODO_HSA: if group memory has initializer, then emit instructions to
  // initialize dynamically
  if (GV->hasInitializer() && canInitHSAILAddressSpace(GV)) {
    BrigEmitGlobalInit(globalVar, (Constant *)GV->getInitializer());
  }
}

static bool isHSAILInstrinsic(StringRef str) {
  if ((HSAILIntrinsic::ID)Intrinsic::not_intrinsic !=
      getIntrinsicForGCCBuiltin("HSAIL", str.data())) return true;
  return str.startswith(StringRef("llvm.HSAIL."));
}

/// Returns true if StringRef is LLVM intrinsic function that define a mapping
/// between LLVM program objects and the source-level objects.
/// See http://llvm.org/docs/SourceLevelDebugging.html#format_common_intrinsics
/// for more details.
static bool isLLVMDebugIntrinsic(StringRef str) {
  return str.equals("llvm.dbg.declare") || str.equals("llvm.dbg.value");
}

/// NOTE: sFuncName is NOT the same as rF.getName()
/// rF may be an unnamed alias of the another function
/// sFuncName is the resolved alias name but rF.getName() is empty
void BRIGAsmPrinter::EmitFunctionLabel(const Function &rF,
                                       const llvm::StringRef sFuncName ) {
  if (isLLVMDebugIntrinsic(rF.getName())) {
    return; // Nothing to do with LLVM debug-related intrinsics
  }
  const Function *F = &rF;
  Type *retType = F->getReturnType();
  FunctionType *funcType = F->getFunctionType();

  HSAIL_ASM::DirectiveFunction fx = brigantine.declFunc(( "&" + sFuncName).str());
  // TODO_HSA: pending BRIG_LINKAGE_STATIC implementation in the Finalizer
  fx.linkage() = F->isExternalLinkage(F->getLinkage()) ?
      Brig::BRIG_LINKAGE_PROGRAM : ( F->isInternalLinkage(F->getLinkage()) ?
     Brig::BRIG_LINKAGE_MODULE : Brig::BRIG_LINKAGE_NONE );

  paramCounter = 0;
  if (retType && (retType->getTypeID() != Type::VoidTyID)) {
    EmitFunctionReturn(retType, false, StringRef(), F->getAttributes().getRetAttributes()
                       .hasAttribute(AttributeSet::ReturnIndex, Attribute::SExt));
  }
  if (funcType) {
    // Loop through all of the parameters and emit the types and
    // corresponding names.
    reg1Counter = 0;
    reg32Counter = 0;
    reg64Counter = 0;
    Function::const_arg_iterator ai = F->arg_begin();
    Function::const_arg_iterator ae = F->arg_end();
    unsigned n = 1;
    for (FunctionType::param_iterator pb = funcType->param_begin(),
         pe = funcType->param_end(); pb != pe; ++pb, ++ai, ++n) {
      assert(ai != ae);
      Type* type = *pb;
      EmitFunctionArgument(type, false, ai->getName(), F->getAttributes().getParamAttributes(n)
                           .hasAttribute(AttributeSet::FunctionIndex, Attribute::SExt));
    }
  }
}

//===------------------------------------------------------------------===//
// Overridable Hooks
//===------------------------------------------------------------------===//

/**
 *
 *
 * @param lMF MachineFunction to print the assembly for
 * @brief parse the specified machineModel function and print
 * out the assembly for all the instructions in the function
 *
 * @return
 */

bool  BRIGAsmPrinter::runOnMachineFunction(MachineFunction &lMF) {
  this->MF = &lMF;
  mMeta->setMF(&lMF);
  mMFI = lMF.getInfo<HSAILMachineFunctionInfo>();
  SetupMachineFunction(lMF);
  const Function *F = MF->getFunction();
  OutStreamer.SwitchSection(getObjFileLowering().SectionForGlobal(F, *Mang, TM));
  m_bIsKernel = HSAIL::isKernelFunc(MF->getFunction());
  mMeta->printHeader(MF->getFunction()->getName());

  // The need to define global samplers is discovered during instruction selection,
  // so we emit them at file scope just before a kernel function is emitted.
  Subtarget->getImageHandles()->finalize();
  EmitSamplerDefs();

  EmitFunctionEntryLabel();
  EmitFunctionBody();

  // Clear local handles from image handles
  Subtarget->getImageHandles()->clearImageArgs();

  return false;
}

void BRIGAsmPrinter::EmitSamplerDefs() {

  HSAILImageHandles* handles = Subtarget->getImageHandles();
  SmallVector<HSAILSamplerHandle*, 16> samplers = handles->getSamplerHandles();

  // Emit global sampler defs
  for (unsigned i = 0; i < samplers.size(); i++) {
    // All sampler defs (samplers with initializers) are global, so we emit
    // them only once.
    if (!samplers[i]->isEmitted()) {

      HSAIL_ASM::DirectiveVariable samplerVar = brigantine.addSampler("&" +
        samplers[i]->getSym(), samplers[i]->isRO() ? Brig::BRIG_SEGMENT_READONLY
                                                   : Brig::BRIG_SEGMENT_GLOBAL);
      samplerVar.align() = Brig::BRIG_ALIGNMENT_8;
      samplerVar.allocation() = Brig::BRIG_ALLOCATION_AGENT;
      samplerVar.linkage() = Brig::BRIG_LINKAGE_MODULE;
      samplerVar.modifier().isDefinition() = 1;
      HSAIL_ASM::OperandSamplerProperties samplerProps = brigantine.append<HSAIL_ASM::OperandSamplerProperties>();
      // HSAIL_ASM::ItemList samplerInit;
      // samplerInit.push_back(samplerProps);
      samplerVar.init() = samplerProps;

      int ocl_init = handles->getSamplerValue(i);

      samplerProps.coord() = (ocl_init & 0x1)
            ? Brig::BRIG_COORD_NORMALIZED
            : Brig::BRIG_COORD_UNNORMALIZED;

      switch (ocl_init & 0x30) {
      default:
      case 0x10:
        samplerProps.filter() = Brig::BRIG_FILTER_NEAREST; // CLK_FILTER_NEAREST
        break;
      case 0x20:
        samplerProps.filter() = Brig::BRIG_FILTER_LINEAR; // CLK_FILTER_LINEAR
        break;
      }

      switch (ocl_init & 0xE) {
      case 0x0 : samplerProps.addressing() = Brig::BRIG_ADDRESSING_UNDEFINED;          break;  // CLK_ADDRESS_NONE
      case 0x2 : samplerProps.addressing() = Brig::BRIG_ADDRESSING_REPEAT;             break;  // CLK_ADDRESS_REPEAT
      case 0x4 : samplerProps.addressing() = Brig::BRIG_ADDRESSING_CLAMP_TO_EDGE;      break;  // CLK_ADDRESS_CLAMP_TO_EDGE
      case 0x6 : samplerProps.addressing() = Brig::BRIG_ADDRESSING_CLAMP_TO_BORDER;    break;  // CLK_ADDRESS_CLAMP
      case 0x8 : samplerProps.addressing() = Brig::BRIG_ADDRESSING_MIRRORED_REPEAT;    break;  // CLK_ADDRESS_MIRRORED_REPEAT
      }

      samplers[i]->setEmitted();
    }
  }
}

bool BRIGAsmPrinter::isMacroFunc(const MachineInstr *MI) {
  if (MI->getOpcode() != HSAIL::target_call) {
    return false;
  }
  const llvm::StringRef &nameRef = MI->getOperand(0).getGlobal()->getName();
  if (nameRef.startswith("barrier")) {
    return true;
  }
  return false;
}

bool BRIGAsmPrinter::isIdentityCopy(const MachineInstr *MI) const {
  if (MI->getNumOperands() != 2) {
    return false;
  }
  switch(MI->getOpcode()) {
    // Bitconvert is a copy instruction
  case HSAIL::bitcvt_f32_u32:
  case HSAIL::bitcvt_u32_f32:
  case HSAIL::bitcvt_f64_u64:
  case HSAIL::bitcvt_u64_f64:
    return MI->getOperand(0).getReg() == MI->getOperand(1).getReg();
  default:
    return false;
  }
}

void BRIGAsmPrinter::emitMacroFunc(const MachineInstr *MI, raw_ostream &O) {
  llvm::StringRef nameRef;
  nameRef = MI->getOperand(0).getGlobal()->getName();
  if (nameRef.startswith("barrier")) {
    O << '\t';
    O << nameRef;
    O << ';';
    return;
  }
}

void BRIGAsmPrinter::EmitBasicBlockStart(const MachineBasicBlock &MBB) {
  std::ostringstream o;
  bool insert_spaces=false;

  if (MBB.pred_empty() || isBlockOnlyReachableByFallthrough(&MBB)) {
    o << "// BB#" << MBB.getNumber() << ":";
    insert_spaces=true;
  } else {
    StringRef name = MBB.getSymbol()->getName();
    brigantine.addLabel(HSAIL_ASM::SRef(name.begin(), name.end()));
  }
  if (const BasicBlock *BB = MBB.getBasicBlock())
    if  (BB->hasName())
      o << ( insert_spaces ? "                                ":"")
        <<"// %" << BB->getName();
  if (o.str().length()) {
    brigantine.addComment(o.str().c_str());
  }

  AsmPrinter::EmitBasicBlockStart(MBB);
}

namespace {
  class autoCodeEmitter {
    MCStreamer                *streamer;
    const HSAIL_ASM::Brigantine *brigantine;
    uint64_t                   lowpc;
    uint64_t                   hipc;

  public:
    autoCodeEmitter(MCStreamer *strm, const HSAIL_ASM::Brigantine *brig):
        streamer(strm), brigantine(brig) {
        lowpc = brigantine->container().code().size();
    }

    ~autoCodeEmitter() {
      hipc = brigantine->container().code().size();
#if 0
      ::printf("Instruction %p emitted to range %08X - %08X\n",
        (const void*)_instruction, (unsigned)_lowpc, (unsigned)_hipc);
#endif
      streamer->SwitchSection(streamer->getContext().getObjectFileInfo()->
        getTextSection());
      assert(lowpc <= hipc);
      // This is the only way to adjust the size of virtual ELF section
      // (type SHT_NOBITS) like .brigcode
      streamer->EmitZeros(hipc - lowpc);
    }

  };
}

bool IsKernel(const MachineInstr *II) {
  std::string sFuncName;
  const MachineBasicBlock * bb = II->getParent();
  if ( bb ) {
    const MachineFunction * mf = bb->getParent();

    if ( mf ) {
      const Function * F = mf->getFunction();
      sFuncName = "&" + F->getName().str();
      return HSAIL::isKernelFunc(F);
    }
  }
  return false;
}

void BRIGAsmPrinter::EmitInstruction(const MachineInstr *II) {
  m_opndList.clear();
  HSAIL_ASM::Inst inst = EmitInstructionImpl(II);
  if (inst) {
    inst.operands() = m_opndList;
  }
}

HSAIL_ASM::Inst BRIGAsmPrinter::EmitInstructionImpl(const MachineInstr *II) {
  // autoCodeEmitter will emit required amount of bytes in corresponding MCSection
  autoCodeEmitter ace(&OutStreamer, &brigantine);

  if (isIdentityCopy(II)) {
    return HSAIL_ASM::Inst();
  }

  if (HSAIL::isAtomicOp(II)) {
    bool hasRet = HSAIL::isRetAtomicOp(II);
    Brig::BrigTypeX btype = getAtomicType(II);
    unsigned brigAtomicOp = hasRet ? Brig::BRIG_OPCODE_ATOMIC :
                                     Brig::BRIG_OPCODE_ATOMICNORET;
    HSAIL_ASM::InstAtomic instAtomic =
        brigantine.addInst<HSAIL_ASM::InstAtomic>(brigAtomicOp, btype);

    instAtomic.atomicOperation() = getAtomicOpcode(II);
    instAtomic.segment() = getAtomicSegment(II);
    instAtomic.memoryOrder() = getAtomicOrder(II);
    instAtomic.memoryScope() = getAtomicScope(II);
    instAtomic.equivClass() = 0;

    if (hasRet)
      BrigEmitOperand(II, 0, instAtomic);
    BrigEmitOperandLdStAddress(II, HSAIL_ATOMIC_OPS::TYPE_INDEX + 1 +
                               (hasRet ? 1 : 0));
    if (HSAIL::isTernaryAtomicOp(II)) {
      BrigEmitOperand(II, II->getNumOperands() - 2, instAtomic);
    }
    if (HSAIL::isTernaryAtomicOp(II) ||
        HSAIL::isBinaryAtomicOp(II)) {
      BrigEmitOperand(II, II->getNumOperands() - 1, instAtomic);
    }
    return instAtomic;
  }

  if (HSAIL::isImageInst(II)) {
    const char *AsmStr = getInstMnemonic(II);
    HSAIL_ASM::InstImage inst = HSAIL_ASM::parseMnemo(AsmStr, brigantine);
    BrigEmitImageInst(II, inst);
    return inst;
  }

  if (HSAIL::isCrosslaneInst(II)) {
    const char *AsmStr = getInstMnemonic(II);
    HSAIL_ASM::Inst inst = HSAIL_ASM::parseMnemo(AsmStr, brigantine);
    BrigEmitVecOperand(II, 0, 4, inst);
    BrigEmitOperand(II, 4, inst);
    return inst;
  }

  switch(II->getOpcode()) {
  default: {
    const char *AsmStr = getInstMnemonic(II);
    HSAIL_ASM::Inst inst = HSAIL_ASM::parseMnemo(AsmStr, brigantine);
    unsigned NumOperands = II->getNumOperands();
    for (unsigned OpNum=0; OpNum != NumOperands; ++OpNum) {
      BrigEmitOperand(II, OpNum, inst);
    }
    return inst;
  }

  case HSAIL::ret:
    return brigantine.addInst<HSAIL_ASM::InstBasic>(Brig::BRIG_OPCODE_RET,Brig::BRIG_TYPE_NONE);

  case HSAIL::arg_scope_start:
    brigantine.startArgScope();
    return HSAIL_ASM::Inst();

  case HSAIL::arg_scope_end:
    brigantine.endArgScope();
    return HSAIL_ASM::Inst();

  case HSAIL::target_call:
    if (isMacroFunc(II)) {
      // TODO_HSA: SINCE THE 'barrier' ONLY IS CURRENTLY HANDLED WE'll SUPPORT THIS LATER
#if 0
      emitMacroFunc(II, O);
#endif
     return HSAIL_ASM::Inst();
    } else {
      MachineInstr::const_mop_iterator oi = II->operands_begin();
      MachineInstr::const_mop_iterator oe = II->operands_end();
      const GlobalValue *gv = (oi++)->getGlobal();;

      // Place a call
      HSAIL_ASM::InstBr call = brigantine.addInst<HSAIL_ASM::InstBr>(
                                 Brig::BRIG_OPCODE_CALL,Brig::BRIG_TYPE_NONE);
      call.width() = Brig::BRIG_WIDTH_ALL;

      HSAIL_ASM::ItemList ret_list;
      for (; oi != oe && oi->isSymbol() ; ++oi) {
        std::string ret("%");
        ret += oi->getSymbolName();
        ret_list.push_back(
          brigantine.findInScopes<HSAIL_ASM::DirectiveVariable>(ret));
      }

      // Return value and argument symbols are delimited with a 0 value.
      assert((oi->isImm() && (oi->getImm() == 0)) ||
             !"Unexpected target call instruction operand list!");

      HSAIL_ASM::ItemList call_paramlist;
      for (++oi; oi != oe; ++oi) {
        if (oi->isSymbol()) {
          std::string op("%");
          op += oi->getSymbolName();
          call_paramlist.push_back(
            brigantine.findInScopes<HSAIL_ASM::DirectiveVariable>(op));
        } else {
          llvm_unreachable("Unexpected target call instruction operand list!");
        }
      }

      m_opndList.push_back(
        brigantine.createCodeList(ret_list));
      m_opndList.push_back(
        brigantine.createExecutableRef(std::string("&") + gv->getName().str()));
      m_opndList.push_back(
        brigantine.createCodeList(call_paramlist));

      return call;
  }

  case HSAIL::lda_32:
  case HSAIL::lda_64: {
    HSAIL_ASM::InstAddr lda = brigantine
      .addInst<HSAIL_ASM::InstAddr>(Brig::BRIG_OPCODE_LDA);
    BrigEmitOperand(II, 0, lda);
    BrigEmitOperandLdStAddress(II, 1);
    lda.segment() = II->getOperand(4).getImm();
    lda.type() = (II->getOpcode() == HSAIL::lda_32) ? Brig::BRIG_TYPE_U32
                                                    : Brig::BRIG_TYPE_U64;
    return lda;
  }

  case HSAIL::ld_32_v1:
  case HSAIL::ld_64_v1:
  case HSAIL::ld_32_ptr32_v1:
  case HSAIL::ld_64_ptr32_v1:
  case HSAIL::rarg_ld_32_ptr32_v1:
  case HSAIL::rarg_ld_64_ptr32_v1:
    return EmitLoadOrStore(II, true, 1);

  case HSAIL::ld_32_v2:
  case HSAIL::ld_64_v2:
  case HSAIL::ld_32_ptr32_v2:
  case HSAIL::ld_64_ptr32_v2:
  case HSAIL::rarg_ld_32_ptr32_v2:
  case HSAIL::rarg_ld_64_ptr32_v2:
    return EmitLoadOrStore(II, true, 2);

  case HSAIL::ld_32_v3:
  case HSAIL::ld_64_v3:
  case HSAIL::ld_32_ptr32_v3:
  case HSAIL::ld_64_ptr32_v3:
  case HSAIL::rarg_ld_32_ptr32_v3:
  case HSAIL::rarg_ld_64_ptr32_v3:
    return EmitLoadOrStore(II, true, 3);

  case HSAIL::ld_32_v4:
  case HSAIL::ld_64_v4:
  case HSAIL::ld_32_ptr32_v4:
  case HSAIL::ld_64_ptr32_v4:
  case HSAIL::rarg_ld_32_ptr32_v4:
  case HSAIL::rarg_ld_64_ptr32_v4:
    return EmitLoadOrStore(II, true, 4);

  case HSAIL::st_32_v1:
  case HSAIL::st_64_v1:
  case HSAIL::st_32_ptr32_v1:
  case HSAIL::st_64_ptr32_v1:
    return EmitLoadOrStore(II, false, 1);

  case HSAIL::st_32_v2:
  case HSAIL::st_64_v2:
  case HSAIL::st_32_ptr32_v2:
  case HSAIL::st_64_ptr32_v2:
    return EmitLoadOrStore(II, false, 2);

  case HSAIL::st_32_v3:
  case HSAIL::st_64_v3:
  case HSAIL::st_32_ptr32_v3:
  case HSAIL::st_64_ptr32_v3:
    return EmitLoadOrStore(II, false, 3);

  case HSAIL::st_32_v4:
  case HSAIL::st_64_v4:
  case HSAIL::st_32_ptr32_v4:
  case HSAIL::st_64_ptr32_v4:
    return EmitLoadOrStore(II, false, 4);

  case HSAIL::arg_decl:
    BrigEmitVecArgDeclaration(II);
    return HSAIL_ASM::Inst();

  case HSAIL::hsail_memfence: {
    HSAIL_ASM::InstMemFence memfence =
        brigantine.addInst<HSAIL_ASM::InstMemFence>(Brig::BRIG_OPCODE_MEMFENCE,
                                                    Brig::BRIG_TYPE_NONE);
    memfence.memoryOrder() =
        II->getOperand(HSAIL_MEMFENCE::MEMORY_ORDER_INDEX).getImm();
    memfence.globalSegmentMemoryScope() =
        II->getOperand(HSAIL_MEMFENCE::GLOBAL_SCOPE_INDEX).getImm();
    memfence.groupSegmentMemoryScope() =
        II->getOperand(HSAIL_MEMFENCE::GROUP_SCOPE_INDEX).getImm();
    memfence.imageSegmentMemoryScope() =
        II->getOperand(HSAIL_MEMFENCE::IMAGE_SCOPE_INDEX).getImm();
    return memfence;
  }
  case HSAIL::ftz_f32: {
  // add_ftz_f32  $dst, $src, 0F00000000
    HSAIL_ASM::InstMod ftz = brigantine.addInst<HSAIL_ASM::InstMod>(
      Brig::BRIG_OPCODE_ADD, Brig::BRIG_TYPE_F32);
    MachineOperand dest = II->getOperand(0);
    MachineOperand src = II->getOperand(1);
    m_opndList.push_back(getBrigReg(dest));
    m_opndList.push_back(getBrigReg(src));
    m_opndList.push_back(brigantine.createImmed(0, Brig::BRIG_TYPE_B32));
    ftz.modifier().round() = Brig::BRIG_ROUND_FLOAT_PLUS_INFINITY;
    ftz.modifier().ftz() = true;
    return ftz;
  }
  }
}

bool BRIGAsmPrinter::doFinalization(Module &M) {

#if 0
  if (getDwarfDebug()) {
    // NamedRegionTimer T(DbgTimerName, DWARFGroupName, TimePassesIsEnabled);
    // Adjust size of fake .brigdirectives section to match actual size of
    // BRIG .directives section
    OutStreamer.SwitchSection(OutStreamer.getContext().getObjectFileInfo()->
                                getDataSection());
    OutStreamer.EmitZeros(brigantine.container().directives().size(), 0);
    // This is not needed at this time, because dwarflinker expects
    // .brigdirectives size to be zero
    DwarfDebug *mDD = getDwarfDebug();
    mDD->endModule();
    delete mDD;
    setDwarfDebug(NULL);
  }
#endif

  // LLVM Bug 9761. Nothing should be emitted after EmitEndOfAsmFile()
  OutStreamer.FinishImpl();

  // Allow the target to emit any magic that it wants at the end of the file,
  // after everything else has gone out.
  EmitEndOfAsmFile(M);

  return false;
}

/// EmitStartOfAsmFile - This virtual method can be overridden by targets
/// that want to emit something at the start of their file.
void BRIGAsmPrinter::EmitStartOfAsmFile(Module &M) {
  if (PrintBeforeBRIG) {
    dbgs() << std::string("*** IR Dump Before ") + getPassName() + " ***";
    M.dump();
  }
  // Clear global variable map
  globalVariableOffsets.clear();

  brigantine.startProgram();
  brigantine.version(Brig::BRIG_VERSION_HSAIL_MAJOR,
    Brig::BRIG_VERSION_HSAIL_MINOR,
    Subtarget->is64Bit() ? Brig::BRIG_MACHINE_LARGE : Brig::BRIG_MACHINE_SMALL,
    Brig::BRIG_PROFILE_FULL);

  //if (usesGCNAtomicCounter()) {
    brigantine.addExtension("amd:gcn");
  //}

  brigantine.addExtension("IMAGE");

  // If we are emitting first instruction that occupied some place in BRIG
  // we should also emit 4 reserved bytes to the MCSection, so that offsets
  // of instructions are the same in the BRIG .code section and MCSection
  OutStreamer.SwitchSection(OutStreamer.getContext().getObjectFileInfo()->
                              getTextSection());
  OutStreamer.EmitZeros(brigantine.container().code().secHeader()->headerByteCount);

  for(Module::const_alias_iterator I = M.alias_begin(), E = M.alias_end();
      I != E; ++I) {
    const Function * F = dyn_cast<Function>(I->getAliasee());
    if (F) {
      funcAliases[F].push_back(I->getName());
    }
  }

  for (Module::const_global_iterator I = M.global_begin(), E = M.global_end();
       I != E; ++I)
    EmitGlobalVariable(I);

  // Emit function declarations
  for (Module::const_iterator I = M.begin(), E = M.end(); I != E; ++I) {
    const Function &F = *I;

    // No declaration for kernels.
    if (HSAIL::isKernelFunc(&F))
      continue;

    std::vector<llvm::StringRef> aliasList;

    if(F.hasName()) {
      aliasList.push_back(F.getName());
    }

    std::vector<llvm::StringRef> aliases = funcAliases[&F];
    if ( !aliases.empty()) {
      aliasList.insert(aliasList.end(), aliases.begin(), aliases.end());
    }

    for (std::vector<llvm::StringRef>::const_iterator AI = aliasList.begin(),
         AE = aliasList.end(); AI != AE; ++AI ) {
      std::string sFuncName = (*AI).str();
      // No declaration for HSAIL instrinsic
      if (!isHSAILInstrinsic(sFuncName) && !F.isIntrinsic()) {
        EmitFunctionLabel(F, sFuncName);
      }
    }
  }
}

/// EmitEndOfAsmFile - This virtual method can be overridden by targets that
/// want to emit something at the end of their file.
void BRIGAsmPrinter::EmitEndOfAsmFile(Module &M) {
  brigantine.endProgram();
  // Clear global variable map
  globalVariableOffsets.clear();
  if(mDwarfStream) {
    // Flush all DWARF data captured
    mDwarfStream->flush();
    // Stop writing to another stream, if any provided
    mDwarfStream->releaseStream();
#if 0
    errs() << "BRIGAsmPrinter: captured " << pos << " DWARF bytes\n";
#endif
    // Actual size of captured DWARF data may be less than the size of
    // mDwarfStream's internal buffer
    const uint64_t dwarfDataSize = mDwarfStream->tell();
    assert(dwarfDataSize && "No DWARF data!"); // sanity check
    if (MMI->hasDebugInfo()) {
      // Obtain reference to data block
      HSAIL_ASM::SRef data = mDwarfStream->getData();
      // \todo1.0 get rid of data copying, stream directly into brig section
      brigantine.container().initSectionRaw(Brig::BRIG_SECTION_INDEX_IMPLEMENTATION_DEFINED, "hsa_debug");
      HSAIL_ASM::BrigSectionImpl& section = brigantine.container().sectionById(Brig::BRIG_SECTION_INDEX_IMPLEMENTATION_DEFINED);
      section.insertData(section.size(), data.begin, data.end);
    }
  }
#if 0
  HSAIL_ASM::dump(bc);
#endif
  // optimizeOperands is not functional as of now
  // bc.optimizeOperands();
  HSAIL_ASM::Validator vld(bc);

  bool isValid=true;
  if (!DisableValidator) {
    isValid= vld.validate();
  }

  if (!isValid) {
    std::cerr << vld.getErrorMsg(NULL) << '\n';
//    HSAIL_ASM::dump(bc);
    if(DumpOnFailFilename.size() > 0) {
      std::string info;

      std::ofstream dumpStream(DumpOnFailFilename.c_str());
      HSAIL_ASM::dump(bc,dumpStream);
    }
    report_fatal_error(
      "\n Brig container validation has failed in BRIGAsmPrinter.cpp\n");
    return;
  }

  if (mBrigStream) {
    if (mTM->HSAILFileType == TargetMachine::CGFT_ObjectFile) {
      // Common case
      // TBD err stream
      RawOstreamWriteAdapter brigAdapter(*mBrigStream, std::cerr);
      HSAIL_ASM::BrigIO::save(bc, HSAIL_ASM::FILE_FORMAT_BRIG, brigAdapter);
    } else {
      HSAIL_ASM::Disassembler disasm(bc);
      disasm.log(std::cerr); // TBD err stream
      // TBD this is incredibly inefficient
      std::stringstream ss;
      int result=disasm.run(ss);
      if (result) {
        assert(!"disasm should not fail if container was validated above");
      }
      const std::string &s = ss.str();
      if (!s.empty()) {
        mBrigStream->write(s.data(), s.size());
      }
    }
  }
  else {
    HSAIL_ASM::BrigStreamer::save(bc, "test_output.brig");
  }
}

static const Type* GetBasicType(const Type* pType) {
  do {
    if(isa<VectorType>(pType)) {
      pType = cast<VectorType>(pType)->getElementType();
      continue;
    }
    if (isa<ArrayType>(pType)) {
      pType = cast<ArrayType>(pType)->getElementType();
      continue;
    }
    if (isa<StructType>(pType)) {
      // 8 bits since getBrigType treats struct as array of bytes
      return llvm::IntegerType::get(pType->getContext(),8);
    }
    break;
  } while(1);
  return pType;
}

HSAIL_ASM::DirectiveVariable BRIGAsmPrinter::EmitLocalVariable(
  const GlobalVariable *GV, Brig::BrigSegment8_t segment) {

  const Type *pElType = GetBasicType(GV->getType()->getElementType());
  uint64_t num_elem = 1;
  OpaqueType OT = GetOpaqueType(pElType);
  Type  *type = GV->getType()->getElementType();
  const DataLayout& DL = getDataLayout();
  if (type->isIntegerTy(1)) {
   // convert boolean variables generated by llvm optimizations
   // to i32 type.
    type = Type::getInt32Ty(GV->getContext());
  }
  if (OT == NotOpaque)
    num_elem = HSAIL::getNumElementsInHSAILType(type, DL);
  HSAIL_ASM::DirectiveVariable var;
  if (num_elem > 1) {
     var = brigantine.addArrayVariable(("%" + GV->getName()).str(),num_elem,
           segment, HSAIL::HSAILgetBrigType(type, Subtarget->is64Bit()));
    // Align arrays at least by 4 bytes
    var.align() = getBrigAlignment(std::max((var.dim() > 1) ? 4U : 0U,
                                            std::max(GV->getAlignment(),
                                                     HSAIL::HSAILgetAlignTypeQualifier(type, getDataLayout(), true))));
  } else {
    var = brigantine.addVariable(("%" + GV->getName()).str(),
      segment, HSAIL::HSAILgetBrigType(type, Subtarget->is64Bit()));
    var.align() = getBrigAlignment(HSAIL::HSAILgetAlignTypeQualifier(type,
                                     getDataLayout(), true));
  }
  var.allocation() = Brig::BRIG_ALLOCATION_AUTOMATIC;
  var.linkage() = Brig::BRIG_LINKAGE_FUNCTION;
  var.modifier().isDefinition() = 1;

  return var;
}

/// EmitFunctionBodyStart - Targets can override this to emit stuff before
/// the first basic block in the function.
void BRIGAsmPrinter::EmitFunctionBodyStart() {
#if 0
  DwarfDebug *mDD = getDwarfDebug();
  if (mDD) {
    //NamedRegionTimer T(DbgTimerName, DWARFGroupName, TimePassesIsEnabled);
    mDD->beginFunction(MF);
  }
#endif

  brigantine.startBody();

  const Function *F = MF->getFunction();

  {
    bool isKernel = HSAIL::isKernelFunc(F);
    if (isKernel) {
      // Emitting block data inside of kernel
      uint32_t id = 0;
      mMeta->setID(id);
      mMeta->setKernel(isKernel);
      ++mBuffer;
      // Preserved this ostream for compatibility only
      std::string ignored_FunStr;
      raw_string_ostream ignored_OFunStr(ignored_FunStr);
      formatted_raw_ostream ignored(ignored_OFunStr);
// D2 does not need to report kernel args info
//      mMeta->processArgMetadata(ignored, mBuffer, isKernel);
      // We have to call processArgMetadata with ostream before we can emit something
      mMeta->brigEmitMetaData(brigantine, id, isKernel);
    }
  }

  // Clear the lists of group variables
  groupVariablesOffsets.clear();

  // Record private/group variable references
  for (MachineFunction::const_iterator block = MF->begin(),
     endBlock = MF->end();  block != endBlock; ++block) {
    for (MachineBasicBlock::const_iterator inst = block->begin(),
      instEnd = block->end(); inst != instEnd; ++inst ) {
      const MachineInstr *MI = inst;
      for (unsigned int opNum = 0; opNum < MI->getNumOperands(); opNum++) {
        const MachineOperand &MO = MI->getOperand(opNum);
        if (MO.getType() == MachineOperand::MO_GlobalAddress) {
          if (const GlobalVariable *GV =
              dyn_cast<GlobalVariable>(MO.getGlobal())) {
            switch(GV->getType()->getAddressSpace()) {
            case HSAILAS::GROUP_ADDRESS:
              groupVariablesOffsets.insert(pvgvo_record(GV, 0));
              break;
            }
          }
        }
      }
    }
  }

  // Fix names for GROUP_ADDRESS
  for (PVGVOffsetMap::iterator i=groupVariablesOffsets.begin(),
                               e=groupVariablesOffsets.end(); i!=e; ++i)
    HSAIL::sanitizeGlobalValueName(const_cast<GlobalVariable*>(i->first));

  // Emit recorded
  for (Module::const_global_iterator I = F->getParent()->global_begin(),
       E = F->getParent()->global_end(); I != E; ++I) {
    pvgvo_iterator II = groupVariablesOffsets.find(I);
    if (II != groupVariablesOffsets.end()) {
         HSAIL_ASM::DirectiveVariable var =
             EmitLocalVariable(II->first, Brig::BRIG_SEGMENT_GROUP);

      II->second = var.brigOffset();
    }
  }

  DataLayout DL = getDataLayout();

  const MachineFrameInfo *MFI = MF->getFrameInfo();
  size_t stack_size = MFI->getOffsetAdjustment() + MFI->getStackSize();

  size_t spill_size =0;
  size_t local_stack_size =0;
  unsigned local_stack_align, spill_align;
  local_stack_align = spill_align = DL.getABIIntegerTypeAlignment(32);

  spillMapforStack.clear();
  LocalVarMapforStack.clear();

  int stk_dim = MFI->getNumObjects();
  int stk_object_indx_begin = MFI->getObjectIndexBegin();
  for (int stk_indx = 0; stk_indx < stk_dim; stk_indx++) {
    int obj_index = stk_object_indx_begin + stk_indx;
    if (!MFI->isDeadObjectIndex(obj_index)) {
      if (MFI->isSpillSlotObjectIndex(obj_index)) {
        unsigned obj_align = MFI->getObjectAlignment(obj_index);
        spill_size = (spill_size + obj_align - 1) / obj_align * obj_align;
        spillMapforStack[MFI->getObjectOffset(obj_index)] = spill_size;
        spill_size += MFI->getObjectSize(obj_index);
        spill_align = std::max(spill_align, obj_align);
      }
      else {
        unsigned obj_offset = MFI->getObjectOffset(obj_index);
        unsigned obj_align = MFI->getObjectAlignment(obj_index);
        local_stack_size = (local_stack_size + obj_align - 1) /
                           obj_align * obj_align;
        LocalVarMapforStack[obj_offset] = local_stack_size;
        unsigned obj_size = MFI->getObjectSize(obj_index);
        for (unsigned cnt = 1 ; cnt < obj_size; cnt++)
          LocalVarMapforStack[obj_offset+cnt] = local_stack_size + cnt;
        local_stack_size  += obj_size;
        local_stack_align = std::max(local_stack_align, obj_align);
      }
    }
  }
  local_stack_size = local_stack_size + MFI->getOffsetAdjustment();
  spill_size = spill_size + MFI->getOffsetAdjustment();

  if (stack_size) {
    // Dimension is in units of type length
    if (local_stack_size) {
      HSAIL_ASM::DirectiveVariable stack_for_locals =
        brigantine.addArrayVariable("%__privateStack", local_stack_size,
                     Brig::BRIG_SEGMENT_PRIVATE, Brig::BRIG_TYPE_U8);
      stack_for_locals.align() = getBrigAlignment(local_stack_align);
      stack_for_locals.allocation() = Brig::BRIG_ALLOCATION_AUTOMATIC;
      stack_for_locals.linkage() = Brig::BRIG_LINKAGE_FUNCTION;
      stack_for_locals.modifier().isDefinition() = 1;
      privateStackBRIGOffset = stack_for_locals.brigOffset();
    }
    if (spill_size) {
      HSAIL_ASM::DirectiveVariable spill = brigantine.addArrayVariable(
        "%__spillStack", spill_size, Brig::BRIG_SEGMENT_SPILL, Brig::BRIG_TYPE_U8);
      spill.align() = getBrigAlignment(spill_align);
      spill.allocation() = Brig::BRIG_ALLOCATION_AUTOMATIC;
      spill.linkage() = Brig::BRIG_LINKAGE_FUNCTION;
      spill.modifier().isDefinition() = 1;
      spillStackBRIGOffset = spill.brigOffset();
    }
  }

  std::string sLabel = "@" + std::string(F->getName()) + "_entry";
  brigantine.addLabel(sLabel);

  retValCounter = 0;
  paramCounter = 0;

#if 0
  if (usesGCNAtomicCounter()) { // TBD095
    HSAIL_ASM::InstBase gcn_region = brigantine.addInst<HSAIL_ASM::InstBase>(
      Brig::BRIG_OPCODE_GCNREGIONALLOC);
    brigantine.appendOperand(gcn_region, brigantine.createImmed(4,
                             Brig::BRIG_TYPE_B32));
  }
#endif
}

/// EmitFunctionBodyEnd - Targets can override this to emit stuff after
/// the last basic block in the function.
void BRIGAsmPrinter::EmitFunctionBodyEnd() {
  autoCodeEmitter ace(&OutStreamer, &brigantine);
  brigantine.endBody();
}

void BRIGAsmPrinter::EmitFunctionReturn(Type* type, bool isKernel,
                                        const StringRef RetName, bool isSExt) {
  std::string SymName("%");
  SymName += RetName.empty() ? "ret" : RetName;
  HSAIL_ASM::SRef ret = SymName;
  reg1Counter = 0;
  reg32Counter = 0;
  reg64Counter = 0;

  // Handle bit return as DWORD
  Type* memType = type->getScalarType();
  if (memType->isIntegerTy(1))
    memType = Type::getInt32Ty(type->getContext());

  // construct return symbol
  HSAIL_ASM::DirectiveVariable retParam;
  if (HSAIL::HSAILrequiresArray(type)) {
    retParam = brigantine.addArrayVariable(ret,
        HSAIL::getNumElementsInHSAILType(type, getDataLayout()),
        Brig::BRIG_SEGMENT_ARG,
        HSAIL::HSAILgetBrigType(memType, Subtarget->is64Bit(), isSExt));
  } else {
    retParam = brigantine.addVariable(ret, Brig::BRIG_SEGMENT_ARG,
                      HSAIL::HSAILgetBrigType(memType, Subtarget->is64Bit(), isSExt));
  }
  retParam.align() = getBrigAlignment(std::max(
    HSAIL::HSAILgetAlignTypeQualifier(memType, getDataLayout(), false),
    HSAIL::HSAILgetAlignTypeQualifier(type, getDataLayout(), false)));
  brigantine.addOutputParameter(retParam);
}

uint64_t BRIGAsmPrinter::EmitFunctionArgument(Type* type, bool isKernel,
                                              const StringRef argName,
                                              bool isSExt) {
  std::ostringstream stream;
  if (argName.empty()) {
    stream << "%arg_p" << paramCounter;
  } else {
    stream << "%" << HSAILParamManager::mangleArg(Mang, argName);
  }
  paramCounter++;

  std::string const name = stream.str();
  const Brig::BrigSegment8_t symSegment = isKernel ? Brig::BRIG_SEGMENT_KERNARG
                                                   : Brig::BRIG_SEGMENT_ARG;

  HSAIL_ASM::DirectiveVariable sym;

  OpaqueType OT = GetOpaqueType(type);
  // Create the symbol
  if (IsImage(OT)) {
    sym = brigantine.addImage(name, symSegment);
    sym.align() = Brig::BRIG_ALIGNMENT_8;
  } else if (OT == Sampler) {
    sym = brigantine.addSampler(name, symSegment);
    sym.align() = Brig::BRIG_ALIGNMENT_8;
  } else {
    // Handle bit argument as DWORD
    Type* memType = type->getScalarType();
    if (memType->isIntegerTy(1))
      memType = Type::getInt32Ty(type->getContext());
    if (HSAIL::HSAILrequiresArray(type)) {
      uint64_t num_elem = HSAIL::getNumElementsInHSAILType(type, getDataLayout());
      // TODO_HSA: w/a for RT bug, remove when RT is fixed.
      // RT passes vec3 as 3 elements, not vec4 as required by OpenCL spec.
      if (isKernel && type->isVectorTy())
        num_elem = type->getVectorNumElements();

      sym = brigantine.addArrayVariable(name, num_elem, symSegment,
                HSAIL::HSAILgetBrigType(memType, Subtarget->is64Bit(), isSExt));
      // TODO_HSA: workaround for RT bug.
      // RT does not read argument alignment from BRIG, so if we align vectors
      // on a full vector size that will cause mismatch between kernarg offsets
      // in RT and finalizer.
      // The line below has to be removed as soon as RT is fixed.
      if (isKernel && type->isVectorTy())
        type = type->getVectorElementType();
    } else {
      sym = brigantine.addVariable(name, symSegment,
                      HSAIL::HSAILgetBrigType(memType, Subtarget->is64Bit(), isSExt));
    }
    sym.align() = getBrigAlignment(
      std::max(HSAIL::HSAILgetAlignTypeQualifier(type, getDataLayout(), false),
               HSAIL::HSAILgetAlignTypeQualifier(memType, getDataLayout(), false)));
  }

  uint64_t rv = sym.brigOffset();
  brigantine.addInputParameter(sym);
  return rv;
}

/// Emit the function signature
void BRIGAsmPrinter::EmitFunctionEntryLabel() {
  // Emit function start
  const Function *F = MF->getFunction();
  Type *retType = F->getReturnType();
  FunctionType *funcType = F->getFunctionType();
  bool isKernel = HSAIL::isKernelFunc(F);
  const HSAILParamManager &PM = MF->getInfo<HSAILMachineFunctionInfo>()->
        getParamManager();
  std::stringstream ss;
  ss << "&" << F->getName();
  HSAIL_ASM::DirectiveExecutable fx;
  if (isKernel) {
    fx = brigantine.declKernel(ss.str());
  } else {
    fx = brigantine.declFunc(ss.str());
  }

  fx.linkage() = F->isExternalLinkage(F->getLinkage()) ?
    Brig::BRIG_LINKAGE_PROGRAM :
    ( F->isInternalLinkage(F->getLinkage()) ? Brig::BRIG_LINKAGE_MODULE
                                            : Brig::BRIG_LINKAGE_NONE);

  // Functions with kernel linkage cannot have output args
  if (!isKernel) {
    if (retType && (retType->getTypeID() != Type::VoidTyID)) {
      EmitFunctionReturn(retType, isKernel, PM.getParamName(*(PM.ret_begin())),
        F->getAttributes().getRetAttributes().hasAttribute(AttributeSet::ReturnIndex, Attribute::SExt));
    }
  }
  if (funcType) {
    // Loop through all of the parameters and emit the types and
    // corresponding names.
    reg1Counter = 0;
    reg32Counter = 0;
    reg64Counter = 0;
    paramCounter = 0;

    // clear arguments mapping
    functionScalarArgumentOffsets.clear();
    functionVectorArgumentOffsets.clear();

    HSAILParamManager::param_iterator Ip = PM.arg_begin();
    HSAILParamManager::param_iterator Ep = PM.arg_end();

    FunctionType::param_iterator pb = funcType->param_begin(),
                                 pe = funcType->param_end();

    if (isKernel && F->hasStructRetAttr()) {
      assert(Ip != Ep && "Invalid struct return fucntion!");
      // If this is a struct-return function, don't process the hidden
      // struct-return argument.
      ++Ip;
      ++pb;
    }

    for (unsigned n = 1; pb != pe; ++pb, ++Ip, ++n) {
      Type* type = *pb;
      assert(Ip != Ep);
      // Obtain argument name
      const char* argName = PM.getParamName(*Ip);
      // Here we will store an offset of DirectiveVariable
      uint64_t argDirectiveOffset = 0;
      argDirectiveOffset = EmitFunctionArgument(type, isKernel, argName,
        F->getAttributes().getParamAttributes(n).hasAttribute(AttributeSet::FunctionIndex, Attribute::SExt));
      functionScalarArgumentOffsets[argName] = argDirectiveOffset;
    }
  }

  // DO NOT need to call endFunc() here it'll be called later on
  // in EmitFunctionBodyEnd().
}

int BRIGAsmPrinter::getHSAILParameterSize(Type* type, HSAIL_ARG_TYPE arg_type) {
  int type_size = 0;

  switch (type->getTypeID()) {
  case Type::VoidTyID:
    break;
  case Type::FloatTyID:
    type_size = 4;
    break;
  case Type::DoubleTyID:
    type_size = 8;
    break;
  case Type::IntegerTyID:
    if (type->isIntegerTy(8)) {
      type_size = 1;
    } else if (type->isIntegerTy(16)) {
      type_size = 2;
    } else if (type->isIntegerTy(32)) {
      type_size = 4;
    } else if (type->isIntegerTy(64)) {
      type_size = 8;
    } else if (type->isIntegerTy(1)) {
      type_size = 1;
    } else {
      type->dump();
      assert(!"Found a case we don't handle!");
    }
    break;
  case Type::PointerTyID:
    if (Subtarget->is64Bit())
      type_size = 8;
    else
      type_size = 4;
    break;
  case Type::VectorTyID:
    type_size = getHSAILParameterSize(type->getScalarType(), arg_type);
    break;
  default:
    type->dump();
    assert(!"Found a case we don't handle!");
    break;
  }
  return type_size;
}

std::string BRIGAsmPrinter::getHSAILReg(Type* type) {
  std::stringstream stream;

  switch (type->getTypeID()) {
  case Type::VoidTyID:
    break;
  case Type::FloatTyID:
    stream << "$s" << reg32Counter++;
    break;
  case Type::DoubleTyID:
    stream << "$d" << reg64Counter++;
    break;
  case Type::IntegerTyID:
    if (type->isIntegerTy(1)) {
      stream << "$c" << reg1Counter++;
    } else if (type->isIntegerTy()
        && type->getScalarSizeInBits() <= 32) {
      stream << "$s" << reg32Counter++;
    } else if (type->isIntegerTy()
        && type->getScalarSizeInBits() <= 64) {
      stream << "$d" << reg64Counter++;
    } else {
      type->dump();
      assert(!"Found a case we don't handle!");
    }
    break;
  case Type::PointerTyID:
    if (Subtarget->is64Bit())
      stream << "$d" << reg64Counter++;
    else
      stream << "$s" << reg32Counter++;
    break;
  default:
    type->dump();
    assert(!"Found a case we don't handle!");
    break;
  };

  return stream.str();
}

/// isBlockOnlyReachableByFallthough - Return true if the basic block has
/// exactly one predecessor and the control transfer mechanism between
/// the predecessor and this block is a fall-through.
bool BRIGAsmPrinter::isBlockOnlyReachableByFallthrough(
     const MachineBasicBlock *MBB) const {
  return AsmPrinter::isBlockOnlyReachableByFallthrough(MBB);
}

//===------------------------------------------------------------------===//
// Dwarf Emission Helper Routines
//===------------------------------------------------------------------===//

/// getISAEncoding - Get the value for DW_AT_APPLE_isa. Zero if no isa
/// encoding specified.
unsigned BRIGAsmPrinter::getISAEncoding() {
  return 0;
}

/// Returns true and the offset of %privateStack BRIG variable, or false
/// if there is no local stack
bool BRIGAsmPrinter::getPrivateStackOffset(uint64_t *privateStackOffset) const {
  if (privateStackBRIGOffset != 0) {
    *privateStackOffset = privateStackBRIGOffset;
    return true;
  }
  return false;
}

/// Returns true and the offset of %spillStack BRIG variable, or false
/// if there is no stack for spills
bool BRIGAsmPrinter::getSpillStackOffset(uint64_t *spillStackOffset) const {
  if (spillStackBRIGOffset != 0) {
    *spillStackOffset = spillStackBRIGOffset;
    return true;
  }
  return false;
}

/// This function is used to translate stack objects' offsets reported by
/// MachineFrameInfo to actual offsets in the %privateStack array
bool BRIGAsmPrinter::getLocalVariableStackOffset(int varOffset,
                                                 int *stackOffset) const {
  stack_map_iterator i = LocalVarMapforStack.find(varOffset);
  if (i != LocalVarMapforStack.end()) {
    *stackOffset = i->second;
    return true;
  }
  return false;
}

/// This function is used to translate stack objects' offsets reported by
/// MachineFrameInfo to actual offsets in the %spill array
bool BRIGAsmPrinter::getSpillVariableStackOffset(int varOffset, int *stackOffset) const {
  stack_map_iterator i =  spillMapforStack.find(varOffset);
  if (i != spillMapforStack.end()) {
    *stackOffset = i->second;
    return true;
  }
  return false;
}

bool BRIGAsmPrinter::getGlobalVariableOffset(const GlobalVariable* GV,
                                             uint64_t *result) const {
  gvo_iterator i = globalVariableOffsets.find(GV);
  if (i == globalVariableOffsets.end()) {
    assert(!"Unknown global variable");
    return false;
  }
  *result =  i->second;
  return true;
}

bool BRIGAsmPrinter::getGroupVariableOffset(const GlobalVariable* GV,
                                            uint64_t *result) const {
  pvgvo_const_iterator i = groupVariablesOffsets.find(GV);
  if (i == groupVariablesOffsets.end()) {
    return false;
  }
  *result =  i->second;
  return true;
}

bool BRIGAsmPrinter::getFunctionScalarArgumentOffset(const std::string& argName,
                                                     uint64_t *result) const {
  fao_iterator i = functionScalarArgumentOffsets.find(argName);
  if (i == functionScalarArgumentOffsets.end()) {
    return false;
  }
  *result = i->second;
  return true;
}

bool BRIGAsmPrinter::getFunctionVectorArgumentOffsets(const std::string& argName,
     VectorArgumentOffsets& result) const {
  fvo_iterator i = functionVectorArgumentOffsets.find(argName);
  if (i == functionVectorArgumentOffsets.end()) {
    return false;
  }
  result = i->second;
  return true;
}

void BRIGAsmPrinter::BrigEmitOperand(const MachineInstr *MI, unsigned opNum, HSAIL_ASM::Inst inst) {

  if (HSAIL::hasAddress(MI)) {
    unsigned addrStart = HSAIL::addressOpNum(MI);
    if (opNum == addrStart) {
      BrigEmitOperandLdStAddress(MI, opNum);
      return;
    }
    if ((opNum > addrStart) &&
        (opNum < addrStart + HSAILADDRESS::ADDRESS_NUM_OPS))
      // Ignore rest of address fields, already emitted.
      return;
  }

  const MachineOperand &MO = MI->getOperand(opNum);

  Brig::BrigType16_t const expType = HSAIL_ASM::getOperandType(inst, m_opndList.size(),
                                       brigantine.getMachineModel(), brigantine.getProfile());

  switch (MO.getType()) {
  default:
    printf("<unknown operand type>"); break;
  case MachineOperand::MO_Register:
    m_opndList.push_back(getBrigReg(MO));
    break;
  case MachineOperand::MO_Immediate:
    if (expType==Brig::BRIG_TYPE_B1) {
      m_opndList.push_back(brigantine.createImmed(MO.getImm() != 0 ? 1 : 0, expType));
    } else {
      m_opndList.push_back(brigantine.createImmed(MO.getImm(), expType));
    }
    break;
  case MachineOperand::MO_FPImmediate: {
    const ConstantFP * CFP = MO.getFPImm();
    if (CFP->getType()->isFloatTy()) {
      m_opndList.push_back(
        brigantine.createImmed(HSAIL_ASM::f32_t::fromRawBits(
          *CFP->getValueAPF().bitcastToAPInt().getRawData()), expType));
    } else if (CFP->getType()->isDoubleTy()) {
      m_opndList.push_back(
        brigantine.createImmed(HSAIL_ASM::f64_t::fromRawBits(
          *CFP->getValueAPF().bitcastToAPInt().getRawData()), expType));
    }
    break;
  }
  case MachineOperand::MO_MachineBasicBlock: {
    std::string sLabel = MO.getMBB()->getSymbol()->getName();
    m_opndList.push_back(brigantine.createLabelRef(sLabel));
    break;
  }
  }
}

void BRIGAsmPrinter::BrigEmitOperandLdStAddress(const MachineInstr *MI, unsigned opNum) 
{
  assert(opNum + 2 < MI->getNumOperands());
  const MachineOperand &base = MI->getOperand(opNum),
    &reg  = MI->getOperand(opNum+1),
    &offset_op  = MI->getOperand(opNum+2);

  // Get offset
  assert(offset_op.isImm());
  int64_t offset = offset_op.getImm();

  // Get [%name]
  std::string base_name;
  if (base.isGlobal()) {
    const GlobalValue *gv = base.getGlobal();
    std::stringstream ss;

    ss << getSymbolPrefix(*gv) << gv->getName().str();
    // Do not add offset since it will already be in offset field
    // see 'HSAILDAGToDAGISel::SelectAddrCommon'

    base_name = ss.str();
  }
  // Special cases for spill and private stack
  else if (base.isImm()) {
    int64_t addr = base.getImm();
    assert(isInt<32>(addr));

    if ((MI->getOpcode() == HSAIL::ld_64_v1 ||
         MI->getOpcode() == HSAIL::ld_64_ptr32_v1) &&
        HSAIL::getBrigType(MI).getImm() == Brig::BRIG_TYPE_SAMP) {
      BrigEmitOperandImage(MI, opNum); // Constant sampler.
      return;
    }
    else if (spillMapforStack.find(addr) != spillMapforStack.end()) {
      base_name = "%__spillStack";
      offset += spillMapforStack[addr];
    }
    else if (LocalVarMapforStack.find(addr) != LocalVarMapforStack.end()) {
      base_name = "%__privateStack";
      offset += LocalVarMapforStack[addr];
    }
    else
      llvm_unreachable(
        "Immediate base address: neither spill, private stack nor sampler handle");
  }
  // Kernel or function argument
  else if (base.isSymbol()) {
    base_name = "%";
    base_name.append(base.getSymbolName());
  }
  // Get [$reg]
  HSAIL_ASM::SRef reg_name;
  if (reg.isReg() && reg.getReg() != 0) {
    reg_name = HSAIL_ASM::SRef(getRegisterName(reg.getReg()));
  }

  // Emit operand
  m_opndList.push_back(brigantine.createRef(base_name, reg_name, offset));
}

HSAIL_ASM::Inst BRIGAsmPrinter::EmitLoadOrStore(const MachineInstr *MI,
                                                bool isLoad,
                                                unsigned vec_size) {
  const MachineMemOperand *MMO = *MI->memoperands_begin();
  unsigned as = HSAIL::getAddrSpace(MI);

  HSAIL_ASM::InstMem inst = brigantine.addInst<HSAIL_ASM::InstMem>
    (isLoad ? Brig::BRIG_OPCODE_LD : Brig::BRIG_OPCODE_ST);

  unsigned BT = HSAIL::getBrigType(MI).getImm();
  if (HSAIL_ASM::isBitType(BT))
    BT = HSAIL_ASM::getUnsignedType(HSAIL_ASM::getBrigTypeNumBits(BT));

  inst.segment()    = getHSAILSegment(as);
  inst.type()       = BT;
  inst.align()      = getBrigAlignment(MMO->getAlignment());
  inst.width()      = isLoad ? Brig::BRIG_WIDTH_1 : Brig::BRIG_WIDTH_NONE;
  inst.equivClass() = 0;
  inst.modifier().isConst() = 0;

  if (vec_size == 1)
    BrigEmitOperand(MI, 0, inst);
  else
    BrigEmitVecOperand(MI, 0, vec_size, inst);
  BrigEmitOperandLdStAddress(MI, vec_size);

  if (EnableUniformOperations && isLoad) {
    // Emit width
    const MachineOperand &width_op = HSAIL::getWidth(MI);
    assert( width_op.isImm());
    unsigned int width  = width_op.getImm();
    assert( width == Brig::BRIG_WIDTH_1        ||
            width == Brig::BRIG_WIDTH_WAVESIZE ||
            width == Brig::BRIG_WIDTH_ALL);
    inst.width() = width;

    // Emit const
    if(HSAILAS::GLOBAL_ADDRESS == as) {
      const MachineOperand &Modifier = HSAIL::getLoadModifierMask(MI);
      assert(Modifier.isImm());
      inst.modifier().isConst() = Modifier.getImm() & Brig::BRIG_MEMORY_CONST;
    }
  }

  return inst;
}

void BRIGAsmPrinter::BrigEmitVecArgDeclaration(const MachineInstr *MI) {
  MachineOperand symb      = MI->getOperand(0);
  MachineOperand brig_type = MI->getOperand(1);
  MachineOperand size      = MI->getOperand(2);
  MachineOperand align     = MI->getOperand(3);

  assert( symb.getType()      == MachineOperand::MO_ExternalSymbol );
  assert( brig_type.getType() == MachineOperand::MO_Immediate );
  assert( size.getType()      == MachineOperand::MO_Immediate );
  assert( align.getType()     == MachineOperand::MO_Immediate );

  std::ostringstream stream;
  stream << "%" << symb.getSymbolName();

  unsigned num_elem = size.getImm();

  Brig::BrigTypeX brigType = (Brig::BrigTypeX)brig_type.getImm();
  HSAIL_ASM::DirectiveVariable vec_arg = (num_elem > 1 ) ?
    brigantine.addArrayVariable(stream.str(), num_elem, Brig::BRIG_SEGMENT_ARG, brigType) :
    brigantine.addVariable(stream.str(), Brig::BRIG_SEGMENT_ARG, brigType);

  vec_arg.align() = getBrigAlignment(align.getImm());
  vec_arg.modifier().isDefinition() = true;
  vec_arg.allocation() = Brig::BRIG_ALLOCATION_AUTOMATIC;
  vec_arg.linkage() = Brig::BRIG_LINKAGE_ARG;

  return;
}

void BRIGAsmPrinter::BrigEmitOperandImage(const MachineInstr *MI, unsigned opNum) {
  MachineOperand object = MI->getOperand(opNum);
  unsigned idx = object.getImm();
  std::string sOp;
  // Indices for image_t and sampler_t args are biased, so now we un-bias them.
  // Note that the biased values rely on biasing performed by
  // HSAILPropagateImageOperands and HSAILISelLowering::LowerFormalArguments.
  if (idx < IMAGE_ARG_BIAS) {
    // This is the initialized sampler.
    HSAILSamplerHandle* hSampler = Subtarget->getImageHandles()->
                                     getSamplerHandle(idx);
    assert(hSampler && "Invalid sampler handle");
    std::string samplerName = hSampler->getSym();
    assert(!samplerName.empty() && "Expected symbol here");
    sOp = "&" + samplerName;
  } else {
    // This is the image
    std::string sym = Subtarget->getImageHandles()->
                        getImageSymbol(idx-IMAGE_ARG_BIAS);
    assert(!sym.empty() && "Expected symbol here");
    sOp = "%" + sym;
  }

  m_opndList.push_back(brigantine.createRef(sOp));
}

HSAIL_ASM::OperandReg BRIGAsmPrinter::getBrigReg(MachineOperand s) {
  assert(s.getType() == MachineOperand::MO_Register);
  return brigantine.createOperandReg(HSAIL_ASM::SRef(
                                       getRegisterName(s.getReg())));
}

void BRIGAsmPrinter::BrigEmitVecOperand(
                            const MachineInstr *MI, unsigned opStart,
                            unsigned numRegs, HSAIL_ASM::Inst inst) {
  assert(numRegs >=2 && numRegs <= 4);
  HSAIL_ASM::ItemList list;
  for(unsigned i=opStart; i<opStart + numRegs; ++i) {
      const MachineOperand &MO = MI->getOperand(i);
      if (MO.isReg()) {
        list.push_back(getBrigReg(MO));
      } else if (MO.isImm()) {
        Brig::BrigType16_t const expType =
          HSAIL_ASM::getOperandType(inst, m_opndList.size(),
            brigantine.getMachineModel(), brigantine.getProfile());
        list.push_back(brigantine.createImmed(MO.getImm(), expType));
      }
  }
  m_opndList.push_back(brigantine.createOperandList(list));
}

void BRIGAsmPrinter::BrigEmitImageInst(const MachineInstr *MI,
                                       HSAIL_ASM::InstImage inst) {
  unsigned opCnt = 0;

  if (inst.geometry() == Brig::BRIG_GEOMETRY_2DDEPTH ||
      inst.geometry() == Brig::BRIG_GEOMETRY_2DADEPTH) 
  {
    BrigEmitOperand(MI, opCnt++, inst);
  } else {
    BrigEmitVecOperand(MI, opCnt, 4, inst);
    opCnt+=4;
  }

    switch(inst.opcode()) {
    case Brig::BRIG_OPCODE_RDIMAGE:
      BrigEmitOperand(MI, opCnt++, inst);
      BrigEmitOperand(MI, opCnt++, inst);
    break;
    case Brig::BRIG_OPCODE_LDIMAGE:
    case Brig::BRIG_OPCODE_STIMAGE:
      BrigEmitOperand(MI, opCnt++, inst);
      break;
    default: ;
    }

  switch(inst.geometry()) {
  case Brig::BRIG_GEOMETRY_1D:
  case Brig::BRIG_GEOMETRY_1DB:
    BrigEmitOperand(MI, opCnt++, inst);
    break;
  case Brig::BRIG_GEOMETRY_1DA:
  case Brig::BRIG_GEOMETRY_2D:
  case Brig::BRIG_GEOMETRY_2DDEPTH:
    BrigEmitVecOperand(MI, opCnt, 2, inst); opCnt += 2;
    break;
  case Brig::BRIG_GEOMETRY_2DA:
  case Brig::BRIG_GEOMETRY_2DADEPTH:
  case Brig::BRIG_GEOMETRY_3D:
    BrigEmitVecOperand(MI, opCnt, 3, inst); opCnt += 3;
    break;
  }
}

bool BRIGAsmPrinter::usesGCNAtomicCounter(void) {
  // TODO_HSA: This introduces another pass over all the instrs in the
  // kernel. Need to find a more efficient way to get this info.
  for (MachineFunction::const_iterator I = MF->begin(), E = MF->end();
       I != E; ++I) {
    for (MachineBasicBlock::const_iterator II = I->begin(), IE = I->end();
         II != IE; ++II) {
      switch (II->getOpcode()) {
      default:
        continue;
      case HSAIL::gcn_atomic_append:
      case HSAIL::gcn_atomic_consume:
        return true;
      }
    }
  }
  return false;
}

char BRIGAsmPrinter::getSymbolPrefix(const GlobalValue& gv) {
  const unsigned AddrSpace = gv.getType()->getAddressSpace();
  return (HSAILAS::PRIVATE_ADDRESS==AddrSpace||
          HSAILAS::GROUP_ADDRESS==AddrSpace) ? '%' : '&';
}

Brig::BrigAlignment8_t BRIGAsmPrinter::getBrigAlignment(unsigned align_value) {
  if (align_value & (align_value-1)) {
    // Round to the next power of 2
    unsigned int i = 1;
    while (i < align_value) i <<= 1;
    align_value = i;
  }
  Brig::BrigAlignment8_t ret = HSAIL_ASM::num2align(align_value);
  assert(ret != Brig::BRIG_ALIGNMENT_LAST && "invalid alignment value");
  return ret;
}

// Force static initialization.
extern "C" void LLVMInitializeBRIGAsmPrinter() {
  RegisterAsmPrinter<BRIGAsmPrinter> X(TheHSAIL_32Target);
  RegisterAsmPrinter<BRIGAsmPrinter> Y(TheHSAIL_64Target);
}

extern "C" void LLVMInitializeHSAILAsmPrinter() {
  LLVMInitializeBRIGAsmPrinter();
}
