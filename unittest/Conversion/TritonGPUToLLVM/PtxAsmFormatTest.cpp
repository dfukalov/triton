#include "triton/Conversion/TritonGPUToLLVM/PtxAsmFormat.h"
#include "mlir/Dialect/Arithmetic/IR/Arithmetic.h"
#include "mlir/IR/Builders.h"
#include "triton/Dialect/Triton/IR/Dialect.h"
#include <gtest/gtest.h>

namespace mlir {
namespace triton {
class PtxAsmFormatTest : public ::testing::Test {
protected:
  static constexpr int numValues = 4;

  PtxAsmFormatTest() {
    ctx.loadDialect<arith::ArithmeticDialect>();

    createValues();
  }

  // Creates the test values.
  void createValues() {
    OpBuilder builder(&ctx);
    builder.setInsertionPointToStart(&block);

    // a b1 value for predicate.
    v[0] = builder.create<arith::ConstantIntOp>(builder.getUnknownLoc(), 1, 1);
    for (int i = 0; i < numValues; i++) {
      v[i + 1] =
          builder.create<arith::ConstantIntOp>(builder.getUnknownLoc(), i, 32);
    }
  }

  MLIRContext ctx;
  Block block;
  Value v[numValues + 1];
};

TEST_F(PtxAsmFormatTest, basic) {
  PTXBuilder builder;

  // Create the operands needed by the instructions in the PTX code.
  auto *cst = builder.newConstantOperand(1);
  auto *val = builder.newOperand(v[1], "=r");

  // create an instruction
  auto &mov = *builder.create("mov.b16");

  mov(val, cst).predicate(v[0]);
  ASSERT_EQ(builder.dump(), "@$1 mov.b16 $0, 0x1;");

  auto values = builder.getAllMLIRArgs();
  ASSERT_EQ(values[0], v[1]); // $0 -> v[1]
  ASSERT_EQ(values[1], v[0]); // $1 -> v[0]

  auto constraints = builder.getConstraints();
  ASSERT_EQ(constraints, "=r,b"); // $0 -> =r, $1 -> b
}

TEST_F(PtxAsmFormatTest, complexInstruction) {
  using triton::CacheModifier;
  using triton::EvictionPolicy;

  PTXBuilder builder;

  int width = 16;
  int nWords = 2;

  Value predicateVal = v[0];
  Value addrVal = v[1];

  auto addr = builder.newAddrOperand(addrVal, "l", 128 /*offset*/);

  bool isVolatile = false;
  auto cache = triton::CacheModifier::CA;
  auto cachePriority = triton::EvictionPolicy::EVICT_FIRST;
  bool hasL2EvictPolicy = true;

  auto &ld =
      builder
          .create<PtxIOInstr>("ld") //
          ->o("volatile", isVolatile)
          .global()
          .o("ca", cache == CacheModifier::CA)
          .o("cg", cache == CacheModifier::CG)
          .o("L1::evict_first", cachePriority == EvictionPolicy::EVICT_FIRST)
          .o("L1::evict_last", cachePriority == EvictionPolicy::EVICT_LAST)
          .o("L1::cache_hint", hasL2EvictPolicy)
          .v(nWords)
          .b(width);

  // Link the instruction to operands
  ld(addr).predicate(predicateVal);

  EXPECT_EQ(
      builder.dump(),
      "@$1 ld.global.ca.L1::evict_first.L1::cache_hint.v2.b16 [ $0 + 128 ];");
  auto values = builder.getAllMLIRArgs();
  EXPECT_EQ(values[0], addrVal);      // $0 -> predicate
  EXPECT_EQ(values[1], predicateVal); // $1 -> addr
  EXPECT_EQ(builder.getConstraints(), "l,b");
}

TEST_F(PtxAsmFormatTest, MultiLinePTX) {
  PTXBuilder builder;

  auto *constVal = builder.newConstantOperand(1);
  auto *valVal0 = builder.newOperand(v[1], "=r");
  auto *valVal1 = builder.newOperand(v[2], "=r");

  auto &mov = *builder.create("mov");

  mov(valVal0, constVal);
  mov(valVal1, constVal);
  mov(valVal1, valVal0);

  EXPECT_EQ(builder.dump(), "mov $0, 0x1;\r\n"
                            "mov $1, 0x1;\r\n"
                            "mov $1, $0;");

  auto values = builder.getAllMLIRArgs();
  EXPECT_EQ(values[0], v[1]); // $0 -> v[1]
  EXPECT_EQ(values[1], v[2]); // $1 -> v[2]
}

} // namespace triton
} // namespace mlir
