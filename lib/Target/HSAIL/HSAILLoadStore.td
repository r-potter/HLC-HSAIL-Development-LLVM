////////////////////////////////////////////////////////////////////////////////
// stof, ftos -- not matched, inserted in MachineInstr lowering
// 32 bit
def global_stof_u32 : HSAILInst<(outs GPR32:$dest), (ins GPR32:$src),
                     "stof_global_u32\t$dest, $src",
                     []>;

def group_stof_u32 : HSAILInst<(outs GPR32:$dest), (ins GPR32:$src),
                     "stof_group_u32\t$dest, $src",
                     []>;

def constant_stof_u32 : HSAILInst<(outs GPR32:$dest), (ins GPR32:$src),
                     "stof_readonly_u32\t$dest, $src",
                     []>;

def private_stof_u32 : HSAILInst<(outs GPR32:$dest), (ins GPR32:$src),
                     "stof_private_u32\t$dest, $src",
                     []>;

def global_ftos_u32 : HSAILInst<(outs GPR32:$dest), (ins GPR32:$src),
                     "ftos_global_u32\t$dest, $src",
                     []>;

def group_ftos_u32 : HSAILInst<(outs GPR32:$dest), (ins GPR32:$src),
                     "ftos_group_u32\t$dest, $src",
                     []>;

def constant_ftos_u32 : HSAILInst<(outs GPR32:$dest), (ins GPR32:$src),
                     "ftos_readonly_u32\t$dest, $src",
                     []>;

def private_ftos_u32 : HSAILInst<(outs GPR32:$dest), (ins GPR32:$src),
                     "ftos_private_u32\t$dest, $src",
                     []>;
// 64 bit
def global_stof_u64 : HSAILInst<(outs GPR64:$dest), (ins GPR64:$src),
                     "stof_global_u64\t$dest, $src",
                     []>;

def group_stof_u64 : HSAILInst<(outs GPR64:$dest), (ins GPR64:$src),
                     "stof_group_u64\t$dest, $src",
                     []>;

def constant_stof_u64 : HSAILInst<(outs GPR64:$dest), (ins GPR64:$src),
                     "stof_readonly_u64\t$dest, $src",
                     []>;

def private_stof_u64 : HSAILInst<(outs GPR64:$dest), (ins GPR64:$src),
                     "stof_private_u64\t$dest, $src",
                     []>;

def global_ftos_u64 : HSAILInst<(outs GPR64:$dest), (ins GPR64:$src),
                     "ftos_global_u64\t$dest, $src",
                     []>;

def group_ftos_u64 : HSAILInst<(outs GPR64:$dest), (ins GPR64:$src),
                     "ftos_group_u64\t$dest, $src",
                     []>;

def constant_ftos_u64 : HSAILInst<(outs GPR64:$dest), (ins GPR64:$src),
                     "ftos_readonly_u64\t$dest, $src",
                     []>;

def private_ftos_u64 : HSAILInst<(outs GPR64:$dest), (ins GPR64:$src),
                     "ftos_private_u64\t$dest, $src",
                     []>;

// stof/ftos for address space casting.
multiclass AddrSpaceCastInst<Intrinsic intr, string asm> {
  def _r_addr32 : HSAILInst<(outs GPR32:$dest), (ins GPR32:$src),
      asm#"_u32_u32"#"\t $dest, $src",
      [(set (i32 GPR32:$dest), (intr (i32 GPR32:$src)))]>, Requires<[In32BitMode]>;

  def _i_addr32 : HSAILInst<(outs GPR32:$dest), (ins i32imm:$src),
      asm#"_u32_u32"#"\t $dest, $src",
      [(set (i32 GPR32:$dest), (intr (i32 imm:$src)))]>, Requires<[In32BitMode]>;

  def _r_addr64 : HSAILInst<(outs GPR64:$dest), (ins GPR64:$src),
      asm#"_u64_u64"#"\t $dest, $src",
      [(set (i64 GPR64:$dest), (intr (i64 GPR64:$src)))]>, Requires<[In64BitMode]>;

  def _i_addr64 : HSAILInst<(outs GPR64:$dest), (ins i64imm:$src),
      asm#"_u64_u64"#"\t $dest, $src",
      [(set (i64 GPR64:$dest), (intr (i64 imm:$src)))]>, Requires<[In64BitMode]>;
}

multiclass FToSInst32<Intrinsic intr, string asm> {
  def _r_addr32 : HSAILInst<(outs GPR32:$dest), (ins GPR32:$src),
      asm#"_u32_u32"#"\t $dest, $src",
      [(set (i32 GPR32:$dest), (intr (i32 GPR32:$src)))]>, Requires<[In32BitMode]>;

  def _i_addr32 : HSAILInst<(outs GPR32:$dest), (ins i32imm:$src),
      asm#"_u32_u32"#"\t $dest, $src",
      [(set (i32 GPR32:$dest), (intr (i32 imm:$src)))]>, Requires<[In32BitMode]>;

  def _r_addr64 : HSAILInst<(outs GPR32:$dest), (ins GPR64:$src),
      asm#"_u32_u64"#"\t $dest, $src",
      [(set (i32 GPR32:$dest), (intr (i64 GPR64:$src)))]>, Requires<[In64BitMode]>;

  def _i_addr64 : HSAILInst<(outs GPR32:$dest), (ins i64imm:$src),
      asm#"_u32_u64"#"\t $dest, $src",
      [(set (i32 GPR32:$dest), (intr (i64 imm:$src)))]>, Requires<[In64BitMode]>;
}

multiclass SToFInst32<Intrinsic intr, string asm> {
  def _r_addr32 : HSAILInst<(outs GPR32:$dest), (ins GPR32:$src),
      asm#"_u32_u32"#"\t $dest, $src",
      [(set (i32 GPR32:$dest), (intr (i32 GPR32:$src)))]>, Requires<[In32BitMode]>;

  def _i_addr32 : HSAILInst<(outs GPR32:$dest), (ins i32imm:$src),
      asm#"_u32_u32"#"\t $dest, $src",
      [(set (i32 GPR32:$dest), (intr (i32 imm:$src)))]>, Requires<[In32BitMode]>;

  def _r_addr64 : HSAILInst<(outs GPR64:$dest), (ins GPR32:$src),
      asm#"_u64_u32"#"\t $dest, $src",
      [(set (i64 GPR64:$dest), (intr (i32 GPR32:$src)))]>, Requires<[In64BitMode]>;

  def _i_addr64 : HSAILInst<(outs GPR64:$dest), (ins i32imm:$src),
      asm#"_u64_u32"#"\t $dest, $src",
      [(set (i64 GPR64:$dest), (intr (i32 imm:$src)))]>, Requires<[In64BitMode]>;
}

defm stof_global  : AddrSpaceCastInst<int_HSAIL_addrspacecast_g2f, "stof_global">;
defm ftos_global  : AddrSpaceCastInst<int_HSAIL_addrspacecast_f2g, "ftos_global">;

defm stof_local   : SToFInst32<int_HSAIL_addrspacecast_l2f, "stof_group">;
defm stof_private : SToFInst32<int_HSAIL_addrspacecast_p2f, "stof_private">;

defm ftos_local   : FToSInst32<int_HSAIL_addrspacecast_f2l, "ftos_group">;
defm ftos_private : FToSInst32<int_HSAIL_addrspacecast_f2p, "ftos_private">;


// segmentp
multiclass SegmentPInst<Intrinsic intr, string asm> {
  def _r_addr32 : HSAILInst<(outs CR:$dest), (ins GPR32:$src),
      asm#"_b1_u32"#"\t $dest, $src",
      [(set (i1 CR:$dest), (intr (i32 GPR32:$src)))]>;

  def _i_addr32 : HSAILInst<(outs CR:$dest), (ins i32imm:$src),
      asm#"_b1_u32"#"\t $dest, $src",
      [(set (i1 CR:$dest), (intr (i32 imm:$src)))]>;

  def _r : HSAILInst<(outs CR:$dest), (ins GPR64:$src),
      asm#"_b1_u64"#"\t $dest, $src",
      [(set (i1 CR:$dest), (intr (i64 GPR64:$src)))]>;

  def _i : HSAILInst<(outs CR:$dest), (ins i64imm:$src),
      asm#"_b1_u64"#"\t $dest, $src",
      [(set (i1 CR:$dest), (intr (i64 imm:$src)))]>;
}

defm segmentp_global  : SegmentPInst<int_HSAIL_segmentp_global, "segmentp_global">;
defm segmentp_local   : SegmentPInst<int_HSAIL_segmentp_local, "segmentp_group">;
defm segmentp_private : SegmentPInst<int_HSAIL_segmentp_private, "segmentp_private">;

let isNotDuplicable = 1, hasCtrlDep = 1, hasSideEffects = 1 in {
  def arg_decl : HSAILInst<(outs ), (ins PtrRC:$v, BrigTypeX:$brig_type, i32imm:$size,
                                     i32imm:$alignnment),
                                     "align $alignnment arg_$brig_type\t$v[$size]", []>;
}

class LdStVectorMap<string op, int size> {
  string opcode   = op;
  int    vec_size = size;
}

def getLdStVectorOpcode : InstrMapping {
  let FilterClass = "LdStVectorMap";
  let RowFields   = ["opcode"];
  let ColFields   = ["vec_size"];
  let KeyCol      = ["1"];
  let ValueCols   = [["1"], ["2"], ["3"], ["4"]];
}

////////////////////////////////////////////////////////////////////////////////
// ld (integer, signed and unsigned, 32-bit and 64-bit;
//     float, 32-bit and 64-bit; addr, 32bit and 64-bit)
multiclass HSAIL_LD<RegisterClass RC, DAGOperand MPtr>
{
  let hasAddress = 1, isLoad = 1, mayLoad = 1, hasSideEffects = 0 in {
    let addressOpNum = 1 in {
      def _v1: HSAILInst<(outs RC:$dest),
                         (ins MPtr:$address, BrigTypeX:$t, BrigWidth:$w,
                              BrigMemoryModifierMask:$m),
                        "ld_$w$m$t\t$dest, $address", []>,
               LdStVectorMap<NAME, 1>;
    }

    let addressOpNum = 2 in {
      def _v2 : HSAILInst<(outs RC:$dest0, RC:$dest1),
                          (ins MPtr:$address, BrigTypeX:$t, BrigWidth:$w,
                               BrigMemoryModifierMask:$m),
                           "ld_v2_$w$m$t\t($dest0, $dest1 ), $address", []>,
               LdStVectorMap<NAME, 2>;
    }
    let addressOpNum = 3 in {
      def _v3 : HSAILInst<(outs RC:$dest0, RC:$dest1, RC:$dest2),
                          (ins MPtr:$address, BrigTypeX:$t, BrigWidth:$w,
                               BrigMemoryModifierMask:$m),
                           "ld_v3_$w$m$t\t($dest0, $dest1, $dest2 ), $address", []>,
                LdStVectorMap<NAME, 3>;
    }
    let addressOpNum = 4 in {
      def _v4 : HSAILInst<(outs RC:$dest0, RC:$dest1, RC:$dest2, RC:$dest3),
                          (ins MPtr:$address, BrigTypeX:$t, BrigWidth:$w,
                               BrigMemoryModifierMask:$m),
                           "ld_v4_$w$m$t\t($dest0, $dest1, $dest2, $dest3 ), $address", []>,
                LdStVectorMap<NAME, 4>;
    }
  }
}

class LoadIsPtr32<PatFrag pf> : PatFrag<(ops node:$address), (pf node:$address),
                            [{ return MemOpHasPtr32(N); }]>;

class LoadIsPtrNN<PatFrag pf> : PatFrag<(ops node:$address), (pf node:$address),
                            [{ return !MemOpHasPtr32(N); }]>;

// For segments where the address is 32/64-bit depending on machine model.
defm ld_32 : HSAIL_LD<GPR32, MEMOP>;
defm ld_64 : HSAIL_LD<GPR64, MEMOP>;

def : Pat<(i32 (LoadIsPtrNN<load>        ADDR:$address)), (ld_32_v1 ADDR:$address, BrigTypeX.U32)>;
def : Pat<(f32 (LoadIsPtrNN<load>        ADDR:$address)), (ld_32_v1 ADDR:$address, BrigTypeX.F32)>;
def : Pat<(i32 (LoadIsPtrNN<extloadi8>   ADDR:$address)), (ld_32_v1 ADDR:$address, BrigTypeX.B8)>;
def : Pat<(i32 (LoadIsPtrNN<sextloadi8>  ADDR:$address)), (ld_32_v1 ADDR:$address, BrigTypeX.S8)>;
def : Pat<(i32 (LoadIsPtrNN<zextloadi8>  ADDR:$address)), (ld_32_v1 ADDR:$address, BrigTypeX.U8)>;
def : Pat<(i32 (LoadIsPtrNN<extloadi16>  ADDR:$address)), (ld_32_v1 ADDR:$address, BrigTypeX.B16)>;
def : Pat<(i32 (LoadIsPtrNN<sextloadi16> ADDR:$address)), (ld_32_v1 ADDR:$address, BrigTypeX.S16)>;
def : Pat<(i32 (LoadIsPtrNN<zextloadi16> ADDR:$address)), (ld_32_v1 ADDR:$address, BrigTypeX.U16)>;
def : Pat<(i64 (LoadIsPtrNN<load>        ADDR:$address)), (ld_64_v1 ADDR:$address, BrigTypeX.U64)>;
def : Pat<(f64 (LoadIsPtrNN<load>        ADDR:$address)), (ld_64_v1 ADDR:$address, BrigTypeX.F64)>;

// For segments where the address is 32-bit in both models.
defm ld_32_ptr32 : HSAIL_LD<GPR32, MEMOP32>;
defm ld_64_ptr32 : HSAIL_LD<GPR64, MEMOP32>;

def : Pat<(i32 (LoadIsPtr32<load>        ADDR:$address)), (ld_32_ptr32_v1 ADDR:$address, BrigTypeX.U32)>;
def : Pat<(f32 (LoadIsPtr32<load>        ADDR:$address)), (ld_32_ptr32_v1 ADDR:$address, BrigTypeX.F32)>;
def : Pat<(i32 (LoadIsPtr32<extloadi8>   ADDR:$address)), (ld_32_ptr32_v1 ADDR:$address, BrigTypeX.B8)>;
def : Pat<(i32 (LoadIsPtr32<sextloadi8>  ADDR:$address)), (ld_32_ptr32_v1 ADDR:$address, BrigTypeX.S8)>;
def : Pat<(i32 (LoadIsPtr32<zextloadi8>  ADDR:$address)), (ld_32_ptr32_v1 ADDR:$address, BrigTypeX.U8)>;
def : Pat<(i32 (LoadIsPtr32<extloadi16>  ADDR:$address)), (ld_32_ptr32_v1 ADDR:$address, BrigTypeX.B16)>;
def : Pat<(i32 (LoadIsPtr32<sextloadi16> ADDR:$address)), (ld_32_ptr32_v1 ADDR:$address, BrigTypeX.S16)>;
def : Pat<(i32 (LoadIsPtr32<zextloadi16> ADDR:$address)), (ld_32_ptr32_v1 ADDR:$address, BrigTypeX.U16)>;
def : Pat<(i64 (LoadIsPtr32<load>        ADDR:$address)), (ld_64_ptr32_v1 ADDR:$address, BrigTypeX.U64)>;
def : Pat<(f64 (LoadIsPtr32<load>        ADDR:$address)), (ld_64_ptr32_v1 ADDR:$address, BrigTypeX.F64)>;

let hasSideEffects = 1, hasCtrlDep = 1 in {
  // It is not safe to move ld_arg as it can be in an argscope
  defm rarg_ld_32_ptr32 : HSAIL_LD<GPR32, MEMOP32>;
  defm rarg_ld_64_ptr32 : HSAIL_LD<GPR64, MEMOP32>;
}

let hasAddress = 1, mayLoad = 1, isPseudo = 1, hasSideEffects = 0 in {
  def ld_b1_ptr32 : HSAILInst<(outs CR:$dest), (ins MEMOP32:$address, BrigTypeX:$t,
                                 BrigWidth:$w, BrigMemoryModifierMask:$m),
                              "ld_b1_$w$m$t\t$dest, $address", []>;
}

////////////////////////////////////////////////////////////////////////////////
// st (integer, singed and unsigned, 32-bit and 64-bit;
//     float, 32-bit and 64-bit; addr, 32bit and 64-bit)
multiclass HSAIL_ST<RegisterClass RC, DAGOperand MPtr>
{
  let hasAddress = 1, isStore = 1, mayStore = 1, hasSideEffects = 0 in {
    let addressOpNum = 1 in {
      def _v1 : HSAILInst<(outs), (ins RC:$src,
                                       MPtr:$address, BrigTypeX:$t),
                          "st_$t\t$src, $address", []>,
                LdStVectorMap<NAME, 1>;
    }

    let addressOpNum = 2 in {
      def _v2 : HSAILInst<(outs), (ins RC:$src0, RC:$src1,
                                       MPtr:$address, BrigTypeX:$t),
                          "st_v2_$t\t($src0, $src1 ), $address", []>,
                LdStVectorMap<NAME, 2>;
    }
    let addressOpNum = 3 in {
      def _v3 : HSAILInst<(outs), (ins RC:$src0, RC:$src1, RC:$src2,
                                       MPtr:$address, BrigTypeX:$t),
                          "st_v3_$t\t($src0, $src1, $src2 ), $address", []>,
                LdStVectorMap<NAME, 3>;
    }
    let addressOpNum = 4 in {
      def _v4 : HSAILInst<(outs), (ins RC:$src0, RC:$src1, RC:$src2, RC:$src3,
                                       MPtr:$address, BrigTypeX:$t),
                          "st_v4_$t\t($src0, $src1, $src2, $src3 ), $address", []>,
                LdStVectorMap<NAME, 4>;
    }
  }
}

class StoreIsPtr32<PatFrag pf> : PatFrag<(ops node:$src0, node:$address), (pf node:$src0, node:$address),
                            [{ return MemOpHasPtr32(N); }]>;

class StoreIsPtrNN<PatFrag pf> : PatFrag<(ops node:$src0, node:$address), (pf node:$src0, node:$address),
                            [{ return !MemOpHasPtr32(N); }]>;

// For segments where the address is 32/64-bit depending on machine model.
defm st_32 : HSAIL_ST<GPR32, MEMOP>;
defm st_64 : HSAIL_ST<GPR64, MEMOP>;

def : Pat<(StoreIsPtrNN<store>         (i32 GPR32:$src0), ADDR:$address), (st_32_v1 GPR32:$src0, ADDR:$address, BrigTypeX.U32)>;
def : Pat<(StoreIsPtrNN<store>         (f32 GPR32:$src0), ADDR:$address), (st_32_v1 GPR32:$src0, ADDR:$address, BrigTypeX.F32)>;
def : Pat<(StoreIsPtrNN<truncstorei8>  (i32 GPR32:$src0), ADDR:$address), (st_32_v1 GPR32:$src0, ADDR:$address, BrigTypeX.U8)>;
def : Pat<(StoreIsPtrNN<truncstorei16> (i32 GPR32:$src0), ADDR:$address), (st_32_v1 GPR32:$src0, ADDR:$address, BrigTypeX.U16)>;
def : Pat<(StoreIsPtrNN<store>         (i64 GPR64:$src0), ADDR:$address), (st_64_v1 GPR64:$src0, ADDR:$address, BrigTypeX.U64)>;
def : Pat<(StoreIsPtrNN<store>         (f64 GPR64:$src0), ADDR:$address), (st_64_v1 GPR64:$src0, ADDR:$address, BrigTypeX.F64)>;

def : Pat<(StoreIsPtrNN<store>         (i32 imm:$src0),   ADDR:$address), (st_32_v1 imm:$src0,   ADDR:$address, BrigTypeX.U32)>;
def : Pat<(StoreIsPtrNN<truncstorei8>  (i32 imm:$src0),   ADDR:$address), (st_32_v1 imm:$src0,   ADDR:$address, BrigTypeX.U8)>;
def : Pat<(StoreIsPtrNN<truncstorei16> (i32 imm:$src0),   ADDR:$address), (st_32_v1 imm:$src0,   ADDR:$address, BrigTypeX.U16)>;
def : Pat<(StoreIsPtrNN<store>         (i64 imm:$src0),   ADDR:$address), (st_64_v1 imm:$src0,   ADDR:$address, BrigTypeX.U64)>;

// For segments where the address is 32-bit in both models.
defm st_32_ptr32 : HSAIL_ST<GPR32, MEMOP32>;
defm st_64_ptr32 : HSAIL_ST<GPR64, MEMOP32>;

def : Pat<(StoreIsPtr32<store>         (i32 GPR32:$src0), ADDR:$address), (st_32_ptr32_v1 GPR32:$src0, ADDR:$address, BrigTypeX.U32)>;
def : Pat<(StoreIsPtr32<store>         (f32 GPR32:$src0), ADDR:$address), (st_32_ptr32_v1 GPR32:$src0, ADDR:$address, BrigTypeX.F32)>;
def : Pat<(StoreIsPtr32<truncstorei8>  (i32 GPR32:$src0), ADDR:$address), (st_32_ptr32_v1 GPR32:$src0, ADDR:$address, BrigTypeX.U8)>;
def : Pat<(StoreIsPtr32<truncstorei16> (i32 GPR32:$src0), ADDR:$address), (st_32_ptr32_v1 GPR32:$src0, ADDR:$address, BrigTypeX.U16)>;
def : Pat<(StoreIsPtr32<store>         (i64 GPR64:$src0), ADDR:$address), (st_64_ptr32_v1 GPR64:$src0, ADDR:$address, BrigTypeX.U64)>;
def : Pat<(StoreIsPtr32<store>         (f64 GPR64:$src0), ADDR:$address), (st_64_ptr32_v1 GPR64:$src0, ADDR:$address, BrigTypeX.F64)>;

def : Pat<(StoreIsPtr32<store>         (i32 imm:$src0),   ADDR:$address), (st_32_ptr32_v1 imm:$src0,   ADDR:$address, BrigTypeX.U32)>;
def : Pat<(StoreIsPtr32<truncstorei8>  (i32 imm:$src0),   ADDR:$address), (st_32_ptr32_v1 imm:$src0,   ADDR:$address, BrigTypeX.U8)>;
def : Pat<(StoreIsPtr32<truncstorei16> (i32 imm:$src0),   ADDR:$address), (st_32_ptr32_v1 imm:$src0,   ADDR:$address, BrigTypeX.U16)>;
def : Pat<(StoreIsPtr32<store>         (i64 imm:$src0),   ADDR:$address), (st_64_ptr32_v1 imm:$src0,   ADDR:$address, BrigTypeX.U64)>;

let hasAddress = 1, mayStore = 1, isPseudo = 1, hasSideEffects = 0 in {
  def st_b1_ptr32 : HSAILInst<(outs), (ins CR:$src0, MEMOP32:$address, BrigTypeX:$t),
                              "st_b1_$t\t$src0, $address", []>;
}

////////////////////////////////////////////////////////////////////////////////
// load memory address

class LdaAddr<RegisterClass RC, DAGOperand MPtr>
      : HSAILInst<(outs RC:$dest), (ins MPtr:$addr, BrigSegment:$segment),
      "lda_$segment\t$dest, $addr", []> {
  let hasAddress = 1;
  let addressOpNum = 1;
  let neverHasSideEffects = 1;
}

def lda_32 : LdaAddr<GPR32, MEMOP32>;
def lda_64 : LdaAddr<GPR64, MEMOP>;

def : Pat<(i32 (IL_lda_group    ADDR:$address)), (lda_32 ADDR:$address, BrigSegment.GROUP)>, Requires<[In32BitMode]>;
def : Pat<(i32 (IL_lda_private  ADDR:$address)), (lda_32 ADDR:$address, BrigSegment.PRIVATE)>, Requires<[In32BitMode]>;

// Patterns for 64 bit mode with conversion of loaded 32 bit address.
// TODO_HSA: The cvt operations are necessary until LLVM supports
//           different pointer widths in address spaces.
def : Pat<(i64 (IL_lda_group    ADDR:$address)), (cvt_u32_u64 (lda_32 ADDR:$address, BrigSegment.GROUP))>, Requires<[In64BitMode]>;
def : Pat<(i64 (IL_lda_private  ADDR:$address)), (cvt_u32_u64 (lda_32 ADDR:$address, BrigSegment.PRIVATE))>, Requires<[In64BitMode]>;

def : Pat<(i32 (IL_lda_flat     ADDR:$address)), (lda_32 ADDR:$address, BrigSegment.FLAT)>, Requires<[In32BitMode]>;
def : Pat<(i64 (IL_lda_flat     ADDR:$address)), (lda_64 ADDR:$address, BrigSegment.FLAT)>, Requires<[In64BitMode]>;
def : Pat<(i32 (IL_lda_global   ADDR:$address)), (lda_32 ADDR:$address, BrigSegment.GLOBAL)>, Requires<[In32BitMode]>;
def : Pat<(i64 (IL_lda_global   ADDR:$address)), (lda_64 ADDR:$address, BrigSegment.GLOBAL)>, Requires<[In64BitMode]>;
def : Pat<(i32 (IL_lda_readonly ADDR:$address)), (lda_32 ADDR:$address, BrigSegment.READONLY)>, Requires<[In32BitMode]>;
def : Pat<(i64 (IL_lda_readonly ADDR:$address)), (lda_64 ADDR:$address, BrigSegment.READONLY)>, Requires<[In64BitMode]>;
