//===- HWToBTOR2.cpp - HW to BTOR2 translation ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//
//
// Converts a hw module to a btor2 format and prints it out
//
//===----------------------------------------------------------------------===//

#include "circt/Conversion/HWToBTOR2.h"
#include "../PassDetail.h"
#include "circt/Dialect/Comb/CombDialect.h"
#include "circt/Dialect/Comb/CombOps.h"
#include "circt/Dialect/Comb/CombVisitors.h"
#include "circt/Dialect/HW/HWAttributes.h"
#include "circt/Dialect/HW/HWModuleGraph.h"
#include "circt/Dialect/HW/HWOps.h"
#include "circt/Dialect/HW/HWPasses.h"
#include "circt/Dialect/HW/HWTypes.h"
#include "circt/Dialect/HW/HWVisitors.h"
#include "circt/Dialect/SV/SVAttributes.h"
#include "circt/Dialect/SV/SVDialect.h"
#include "circt/Dialect/SV/SVOps.h"
#include "circt/Dialect/SV/SVTypes.h"
#include "circt/Dialect/SV/SVVisitors.h"
#include "circt/Dialect/Seq/SeqDialect.h"
#include "circt/Dialect/Seq/SeqOps.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/Support/raw_ostream.h"
#include <sstream>

using namespace circt;
using namespace hw;

namespace {
// The goal here is to traverse the operations in order and convert them one by
// one into btor2
struct ConvertHWToBTOR2Pass
    : public ConvertHWToBTOR2Base<ConvertHWToBTOR2Pass>,
      public comb::CombinationalVisitor<ConvertHWToBTOR2Pass>,
      public sv::Visitor<ConvertHWToBTOR2Pass>,
      public hw::TypeOpVisitor<ConvertHWToBTOR2Pass> {
public:
  ConvertHWToBTOR2Pass(raw_ostream &os) : os(os) {}
  // Executes the pass
  void runOnOperation() override;

private:
  // Output stream in which the btor2 will be emitted
  raw_ostream &os;

  // Create a counter that attributes a unique id to each generated btor2 line
  size_t lid = 1; // btor2 line identifiers usually start at 1
  size_t nclocks = 0;

  // Create maps to keep track of lid associations
  // We need these in order to reference results as operands in btor2

  // Keeps track of the ids associated to each declared sort
  // This is used in order to guarantee that sorts are unique and to allow for
  // instructions to reference the given sorts (key: width, value: LID)
  DenseMap<std::pair<size_t, size_t>, size_t> sortToLIDMap;
  // Keeps track of {constant, width} -> LID mappings
  // This is used in order to avoid duplicating constant declarations
  // in the output btor2. It is also useful when tracking
  // constants declarations that aren't tied to MLIR ops.
  DenseMap<APInt, size_t> constToLIDMap;
  // Keeps track of the most recent update line for each operation
  // This allows for operations to be used throughout the btor file
  // with their most recent expression. Btor uses unique identifiers for each
  // instruction, so we need to have an association between those and MLIR Ops.
  DenseMap<Operation *, size_t> opLIDMap;
  // Keeps track of operation aliases. This is used for wire inlining, as
  // btor2 does not have the concept of a wire. This means that wires in
  // hw will simply create an alias for the operation that will point to
  // the same LID as the original op.
  // key: alias, value: original op
  DenseMap<Operation *, Operation *> opAliasMap;
  // Stores the LID of the associated input.
  // This holds a similar function as the opLIDMap but keeps
  // track of block argument index -> LID mappings
  DenseMap<size_t, size_t> inputLIDs;
  DenseMap<Operation *, size_t> memCurrentViewLIDs;
  // Stores all of the register declaration ops.
  // This allows for the emission of transition arcs for the regs
  // to be deferred to the end of the pass.
  // This is necessary, as we need to wait for the `next` operation to
  // have been converted to btor2 before we can emit the transition.
  SmallVector<Operation *> regOps;
  SmallVector<hw::PortInfo> outputPorts;
  SmallVector<Operation *> memOps;

  // Used to perform a DFS search through the module to declare all operands
  // before they are used
  llvm::SmallMapVector<Operation *, OperandRange::iterator, 16> worklist;

  // Keeps track of operations that have been declared
  DenseSet<Operation *> handledOps;

  // Constants used during the conversion
  static constexpr size_t noLID = -1UL;
  static constexpr int64_t noWidth = -1L;

  /// Field helper functions
public:
  // Checks if an operation was declared
  // If so, its lid will be returned
  // Otherwise a new lid will be assigned to the op
  size_t getOpLID(Operation *op) {
    // Look for the original operation declaration
    // Make sure that wires are considered when looking for an lid
    Operation *defOp = getOpAlias(op);
    auto &f = opLIDMap[defOp];

    // If the op isn't associated to an lid, assign it a new one
    if (!f)
      f = lid++;
    return f;
  }

  // Associates the current lid to an operation
  // The LID is then incremented to maintain uniqueness
  size_t setOpLID(Operation *op) {
    size_t oplid = lid++;
    opLIDMap[op] = oplid;
    return oplid;
  }

  // Checks if an operation was declared
  // If so, its lid will be returned
  // Otherwise -1 will be returned
  size_t getOpLID(Value value) {
    // Check for an operation alias
    Operation *defOp = getOpAlias(value.getDefiningOp());

    if (auto it = opLIDMap.find(defOp); it != opLIDMap.end())
      return it->second;

    // Check for special case where op is actually a port
    // To do so, we start by checking if our operation is a block argument
    if (BlockArgument barg = dyn_cast<BlockArgument>(value)) {
      // Extract the block argument index and use that to get the line number
      size_t argIdx = barg.getArgNumber();

      // Check that the extracted argument is in range before using it
      if (auto it = inputLIDs.find(argIdx); it != inputLIDs.end())
        return it->second;
    }

    // Return -1 if no LID was found
    return noLID;
  }

private:
  // Checks if an operation has an alias. This is the case for wires
  // If so, the original operation is returned
  // Otherwise the argument is returned as it is the original op
  Operation *getOpAlias(Operation *op) {

    // Remove the alias until none are left (for wires of wires of wires ...)
    if (auto it = opAliasMap.find(op); it != opAliasMap.end()) {
      // check for aliases of inputs
      if (!it->second) {
        op->emitError("BTOR2 emission does not support for wires of inputs!");
        return op;
      }
      return it->second;
    }

    // If the op isn't an alias then simply return it
    return op;
  }

  std::pair<size_t, size_t> encodeBitVecSort(size_t w) {
    return std::make_pair(0, w);
  }

  std::pair<size_t, size_t> encodeArraySort(size_t depth, size_t width) {
    return std::make_pair(llvm::Log2_64_Ceil(depth), width);
  }

  void updateMemView(seq::FirMemOp op, size_t LID) {
    memCurrentViewLIDs[op] = LID;
  }

  void updateMemView(Value op, size_t LID) {
    memCurrentViewLIDs[op.getDefiningOp()] = LID;
  }

  size_t getArrayStateLID(Value op) {
    return memCurrentViewLIDs[op.getDefiningOp()];
  }

  size_t getArrayStateLID(Operation *op) { return memCurrentViewLIDs[op]; }

  // Updates or creates an entry for the given operation
  // associating it with the current lid
  void setOpAlias(Operation *alias, Operation *op) {
    opAliasMap[alias] = getOpAlias(op);
  }

  // Checks if a sort was declared with the given width
  // If so, its lid will be returned
  // Otherwise -1 will be returned
  size_t getSortLID(size_t w) {
    if (auto it = sortToLIDMap.find(encodeBitVecSort(w));
        it != sortToLIDMap.end())
      return it->second;

    // If no lid was found return -1
    return noLID;
  }

  size_t getSortLID(hw::ArrayType type) {
    return getSortLID(encodeArraySort(type));
  }

  size_t getSortLID(seq::FirMemType type) {
    return getSortLID(encodeArraySort(type));
  }

  size_t getSortLID(std::pair<size_t, size_t> encoding) {
    if (auto it = sortToLIDMap.find(encoding); it != sortToLIDMap.end())
      return it->second;

    // If no lid was found return -1
    return noLID;
  }

  // Associate the sort with a new lid
  size_t setSortLID(size_t w) {
    size_t sortlid = lid;
    // Add the width to the declared sorts along with the associated line id
    sortToLIDMap[encodeBitVecSort(w)] = lid++;
    return sortlid;
  }

  size_t setSortLID(std::pair<size_t, size_t> encoding) {
    size_t sortlid = lid;
    // Add the width to the declared sorts along with the associated line id
    sortToLIDMap[encoding] = lid++;
    return sortlid;
  }

  // Checks if a constant of a given size has been declared.
  // If so, its lid will be returned.
  // Otherwise -1 will be returned.
  size_t getConstLID(int64_t val, size_t w) {
    if (auto it = constToLIDMap.find(APInt(w, val)); it != constToLIDMap.end())
      return it->second;

    // if no lid was found return -1
    return noLID;
  }

  // Associates a constant declaration to a new lid
  size_t setConstLID(int64_t val, size_t w) {
    size_t constlid = lid;
    // Keep track of this value in a constant declaration tracker
    constToLIDMap[APInt(w, val)] = lid++;
    return constlid;
  }

  /// String generation helper functions

  // Generates a sort declaration instruction given a type ("bitvec" or array)
  // and a width.
  void genSort(StringRef type, size_t width) {
    // Check that the sort wasn't already declared
    if (getSortLID(width) != noLID) {
      return; // If it has already been declared then return an empty string
    }

    size_t sortlid = setSortLID(width);

    // Build and return a sort declaration
    os << sortlid << " "
       << "sort"
       << " " << type << " " << width << "\n";
  }

  void genArraySort(std::pair<size_t, size_t> encoding) {
    if (getSortLID(encoding) != noLID) {
      return;
    }
    auto [indexWidth, dataWidth] = encoding;
    genSort("bitvec", indexWidth);
    genSort("bitvec", dataWidth);
    size_t indexSID = getSortLID(indexWidth);
    size_t dataSID = getSortLID(dataWidth);
    size_t sortlid = setSortLID(encoding);
    os << sortlid << " "
       << "sort"
       << " "
       << "array"
       << " " << indexSID << " " << dataSID << "\n";
  }

  // Generates an input declaration given a sort lid and a name.
  void genInput(size_t inlid, size_t width, StringRef name) {
    // Retrieve the lid associated with the sort (sid)
    size_t sid = getSortLID(width);

    // Generate input declaration
    os << inlid << " "
       << "input"
       << " " << sid << " " << name << "\n";
  }

  // Generates a constant declaration given a value, a width and a name.
  void genConst(int64_t value, size_t width, Operation *op) {
    // For now we're going to assume that the name isn't taken, given that hw is
    // already in SSA form
    genConst(value, width, getOpLID(op));
  }

  void genConst(int64_t value, size_t width, size_t opLID) {
    // Retrieve the lid associated with the sort (sid)
    size_t sid = getSortLID(width);

    os << opLID << " "
       << "constd"
       << " " << sid << " " << value << "\n";
  }

  // Generates a zero constant expression
  size_t genZero(size_t width) {
    // Check if the constant has been created yet
    size_t zlid = getConstLID(0, width);
    if (zlid != noLID)
      return zlid;

    // Retrieve the lid associated with the sort (sid)
    size_t sid = getSortLID(width);

    // Associate an lid to the new constant
    size_t constlid = setConstLID(0, width);

    // Build and return the zero btor instruction
    os << constlid << " "
       << "zero"
       << " " << sid << "\n";
    return constlid;
  }

  // Generates an init statement, which allows for the use of powerOnValue
  // operands in compreg registers
  void genInit(Operation *reg, Value initVal, int64_t width) {
    // Retrieve the various identifiers we require for this
    size_t regLID = getOpLID(reg);
    size_t sid = getSortLID(width);
    size_t initValLID = getOpLID(initVal);

    // Build and emit the string (the lid here doesn't need to be associated to
    // an op as it won't be used)
    os << lid++ << " "
       << "init"
       << " " << sid << " " << regLID << " " << initValLID << "\n";
  }

  void genBinOp(StringRef inst, size_t opLID, size_t op1LID, size_t op2LID,
                size_t width) {
    size_t sid = getSortLID(width);
    // Build and return the string
    os << opLID << " " << inst << " " << sid << " " << op1LID << " " << op2LID
       << "\n";
  }

  // Generates a binary operation instruction given an op name, two operands and
  // a result width.
  void genBinOp(StringRef inst, Operation *binop, Value op1, Value op2,
                size_t width) {
    // Set the LID for this operation
    size_t opLID = getOpLID(binop);

    // Assuming that the operands were already emitted
    // Find the LIDs associated to the operands
    size_t op1LID = getOpLID(op1);
    size_t op2LID = getOpLID(op2);
    genBinOp(inst, opLID, op1LID, op2LID, width);
  }

  void genSlice(size_t opLID, size_t op0LID, size_t lowbit, int64_t width) {
    size_t sid = getSortLID(width);
    // Build and return the slice instruction
    os << opLID << " "
       << "slice"
       << " " << sid << " " << op0LID << " " << (lowbit + width - 1) << " "
       << lowbit << "\n";
  }

  // Generates a slice instruction given an operand, the lowbit, and the width
  void genSlice(Operation *srcop, Value op0, size_t lowbit, int64_t width) {
    // Assign a LID to this operation
    size_t opLID = getOpLID(srcop);
    // Assuming that the operand has already been emitted
    // Find the LID associated to the operand
    size_t op0LID = getOpLID(op0);
    genSlice(opLID, op0LID, lowbit, width);
  }

  // Generates a constant declaration given a value, a width and a name
  void genUnaryOp(Operation *srcop, Operation *op0, StringRef inst,
                  size_t width) {
    // Register the source operation with the current line id
    size_t opLID = getOpLID(srcop);

    // Retrieve the lid associated with the sort (sid)
    size_t sid = getSortLID(width);

    // Assuming that the operand has already been emitted
    // Find the LID associated to the operand
    size_t op0LID = getOpLID(op0);

    os << opLID << " " << inst << " " << sid << " " << op0LID << "\n";
  }

  // Generates a constant declaration given a value, a width and a name
  void genUnaryOp(Operation *srcop, Value op0, StringRef inst, size_t width) {
    genUnaryOp(srcop, op0.getDefiningOp(), inst, width);
  }

  void genUnaryOp(size_t opLID, size_t op0LID, StringRef inst, size_t width) {
    size_t sid = getSortLID(width);
    os << opLID << " " << inst << " " << sid << " " << op0LID << "\n";
  }

  // Generate a btor2 assertion given an assertion operation
  // Note that a predicate inversion must have already been generated at this
  // point
  void genBad(Operation *assertop) {
    // Start by finding the expression lid
    size_t assertLID = getOpLID(assertop);

    // Build and return the btor2 string
    // Also update the lid as this instruction is not associated to an mlir op
    os << lid++ << " "
       << "bad"
       << " " << assertLID << "\n";
  }

  // Generate a btor2 constraint given an expression from an assumption
  // operation
  void genConstraint(Value expr) {
    // Start by finding the expression lid
    size_t exprLID = getOpLID(expr);

    genConstraint(exprLID);
  }

  // Generate a btor2 constraint given an expression from an assumption
  // operation
  void genConstraint(size_t exprLID) {
    // Build and return the btor2 string
    // Also update the lid as this instruction is not associated to an mlir op
    os << lid++ << " "
       << "constraint"
       << " " << exprLID << "\n";
  }

  // Generate an ite instruction (if then else) given a predicate, two values
  // and a res width
  void genIte(Operation *srcop, Value cond, Value t, Value f, int64_t width) {
    // Retrieve the operand lids, assuming they were emitted
    size_t condLID = getOpLID(cond);
    size_t tLID = getOpLID(t);
    size_t fLID = getOpLID(f);

    genIte(srcop, condLID, tLID, fLID, width);
  }

  // Generate an ite instruction (if then else) given a predicate, two values
  // and a res width
  void genIte(Operation *srcop, size_t condLID, size_t tLID, size_t fLID,
              int64_t width) {
    // Register the source operation with the current line id
    size_t opLID = getOpLID(srcop);
    genIte(opLID, condLID, tLID, fLID, width);
  }

  void genIte(size_t opLID, size_t condLID, size_t tLID, size_t fLID,
              std::pair<size_t, size_t> arrayType) {
    size_t sid = getSortLID(arrayType);
    // Build and return the ite instruction
    os << opLID << " "
       << "ite"
       << " " << sid << " " << condLID << " " << tLID << " " << fLID << "\n";
  }

  void genIte(size_t opLID, size_t condLID, size_t tLID, size_t fLID,
              int64_t width) {
    size_t sid = getSortLID(width);
    // Build and return the ite instruction
    os << opLID << " "
       << "ite"
       << " " << sid << " " << condLID << " " << tLID << " " << fLID << "\n";
  }

  // Generate a logical implication given a lhs and a rhs
  void genImplies(Operation *srcop, Value lhs, Value rhs) {
    // Retrieve LIDs for the lhs and rhs
    size_t lhsLID = getOpLID(lhs);
    size_t rhsLID = getOpLID(rhs);

    genImplies(srcop, lhsLID, rhsLID);
  }

  // Generate a logical implication given a lhs and a rhs
  void genImplies(Operation *srcop, size_t lhsLID, size_t rhsLID) {
    // Register the source operation with the current line id
    size_t opLID = getOpLID(srcop);

    // Retrieve the lid associated with the sort (sid)
    size_t sid = getSortLID(1);

    // Build and emit the implies operation
    os << opLID << " "
       << "implies"
       << " " << sid << " " << lhsLID << " " << rhsLID << "\n";
  }

  // Generates a state instruction given a width and a name
  void genState(Operation *srcop, int64_t width, StringRef name) {
    // Register the source operation with the current line id
    size_t opLID = getOpLID(srcop);

    // Retrieve the lid associated with the sort (sid)
    size_t sid = getSortLID(width);

    // Build and return the state instruction
    os << opLID << " "
       << "state"
       << " " << sid << " " << name << "\n";
  }

  // Generates a next instruction, given a width, a state LID, and a next value
  // LID
  void genNext(Operation *reg, size_t nextLID, int64_t width) {
    // Retrieve the lid associated with the sort (sid)
    size_t sid = getSortLID(width);

    // Retrieve the LIDs associated to reg and next
    size_t regLID = getOpLID(reg);

    // Build and return the next instruction
    // Also update the lid as this instruction is not associated to an mlir op
    os << lid++ << " "
       << "next"
       << " " << sid << " " << regLID << " " << nextLID << "\n";
  }

  void genNext(Operation *reg, size_t nextLID,
               std::pair<size_t, size_t> encoding) {
    // Retrieve the lid associated with the sort (sid)
    size_t sid = getSortLID(encoding);

    // Retrieve the LIDs associated to reg and next
    size_t regLID = getOpLID(reg);

    // Build and return the next instruction
    // Also update the lid as this instruction is not associated to an mlir op
    os << lid++ << " "
       << "next"
       << " " << sid << " " << regLID << " " << nextLID << "\n";
  }

  // Returns a tuple of array index_width and data_width
  std::pair<size_t, size_t> encodeArraySort(hw::ArrayType type) {
    size_t dataWidth = hw::getBitWidth(type.getElementType());
    return encodeArraySort(type.getNumElements(), dataWidth);
  }

  std::pair<size_t, size_t> encodeArraySort(seq::FirMemType type) {
    return encodeArraySort(type.getDepth(), type.getWidth());
  }

  void requireArraySort(mlir::Type type) {
    auto encoding = encodeArraySort(dyn_cast<hw::ArrayType>(type));
    genArraySort(encoding);
  }

  // Verifies that the sort required for the given operation's btor2 emission
  // has been generated
  int64_t requireSort(mlir::Type type) {
    // Start by figuring out what sort needs to be generated
    int64_t width = hw::getBitWidth(type);

    // Sanity check: getBitWidth can technically return -1 it is a type with no
    // width (like a clock). This shouldn't be allowed as width is required to
    // generate a sort
    assert(width != noWidth);

    // Generate the sort regardles of resulting width (nothing will be added if
    // the sort already exists)
    genSort("bitvec", width);
    return width;
  }

  void finalizeRegVisit(Operation *op) {
    int64_t width;
    Value next, reset, resetVal;

    // Extract the operands depending on the register type
    if (auto reg = dyn_cast<seq::CompRegOp>(op)) {
      width = hw::getBitWidth(reg.getType());
      next = reg.getInput();
      reset = reg.getReset();
      resetVal = reg.getResetValue();
    } else if (auto reg = dyn_cast<seq::FirRegOp>(op)) {
      width = hw::getBitWidth(reg.getType());
      next = reg.getNext();
      reset = reg.getReset();
      resetVal = reg.getResetValue();
    } else {
      op->emitError("Invalid register operation !");
      return;
    }

    genSort("bitvec", width);

    // Next should already be associated to an LID at this point
    // As we are going to override it, we need to keep track of the original
    // instruction
    size_t nextLID = noLID;

    // We need to check if the next value is a port to avoid nullptrs
    // To do so, we start by checking if our operation is a block argument
    if (BlockArgument barg = dyn_cast<BlockArgument>(next)) {
      // Extract the block argument index and use that to get the line number
      size_t argIdx = barg.getArgNumber();

      // Check that the extracted argument is in range before using it
      nextLID = inputLIDs[argIdx];

    } else {
      nextLID = getOpLID(next);
    }

    // Check if the register has a reset
    if (reset) {
      size_t resetValLID = noLID;

      // Check if the reset signal is a port to avoid nullptrs (as done above
      // with next)
      size_t resetLID = noLID;
      if (BlockArgument barg = dyn_cast<BlockArgument>(reset)) {
        // Extract the block argument index and use that to get the line
        // number
        size_t argIdx = barg.getArgNumber();

        // Check that the extracted argument is in range before using it
        resetLID = inputLIDs[argIdx];

      } else {
        resetLID = getOpLID(reset);
      }

      // Check for a reset value, if none exists assume it's zero
      if (resetVal)
        resetValLID = getOpLID(resetVal.getDefiningOp());
      else
        resetValLID = genZero(width);

      // Sanity check: at this point the next operation should have had it's
      // btor2 counterpart emitted if not then something terrible must have
      // happened.
      assert(nextLID != noLID);
      size_t guardedLID = lid;
      // Generate the ite for the register update reset condition
      // i.e. reg <= reset ? 0 : next
      genIte(lid++, resetLID, resetValLID, nextLID, width);
      nextLID = guardedLID;
    } else {
      // Sanity check: next should have been assigned
      if (nextLID == noLID) {
        next.getDefiningOp()->emitError(
            "Register input does not point to a valid op!");
        return;
      }
    }

    // Finally generate the next statement
    genNext(op, nextLID, width);
  }

  void finalizeArrayUpdate(Operation *op) {
    auto type = dyn_cast<seq::FirMemOp>(op).getType();
    auto encoding = encodeArraySort(type);
    genNext(op, memCurrentViewLIDs[op], encoding);
  }

public:
  /// Visitor Methods used later on for pattern matching

  // Visitor for the inputs of the module.
  // This will generate additional sorts and input declaration explicitly for
  // btor2 Note that outputs are ignored in btor2 as they do not contribute to
  // the final assertions
  void visit(hw::PortInfo &port) {
    // Separate the inputs from outputs and generate the first btor2 lines for
    // input declaration We only consider ports with an explicit bit-width (so
    // ignore clocks)
    if (port.isInput() && !isa<seq::ClockType>(port.type)) {
      // Generate the associated btor declaration for the inputs
      StringRef iName = port.getName();

      // Guarantees that a sort will exist for the generation of this port's
      // translation into btor2
      int64_t w = requireSort(port.type);

      // Save lid for later
      size_t inlid = lid;

      // Record the defining operation's line ID (the module itself in the case
      // of ports)
      inputLIDs[port.argNum] = lid;

      // Increment the lid to keep it unique
      lid++;

      genInput(inlid, w, iName);
    }
  }

  // Emits the associated btor2 operation for a constant. Note that for
  // simplicity, we will only emit `constd` in order to avoid bit-string
  // conversions
  void visitTypeOp(hw::ConstantOp op) {
    // Make sure that a sort has been created for our operation
    int64_t w = requireSort(op.getType());

    // Prepare for for const generation by extracting the const value and
    // generting the btor2 string
    int64_t value = op.getValue().getSExtValue();
    genConst(value, w, op);
  }

  void visit(hw::OutputOp op) {
    for (size_t i = 0; i < outputPorts.size(); i++) {
      genOutput(op, outputPorts[i], op.getOperand(i));
    }
  }

  void genOutput(Operation *op, hw::PortInfo port, Value value) {
    StringRef portName = port.getName();
    size_t valueLID = getOpLID(value);
    os << lid++ << " "
       << "output"
       << " " << valueLID << " " << portName << "\n";
  }

  // Wires can generally be ignored in bto2, however we do need
  // to keep track of the new alias it creates
  void visit(hw::WireOp op) {
    // Retrieve the aliased operation
    Operation *defOp = op.getOperand().getDefiningOp();
    // Wires don't output anything so just record alias
    setOpAlias(op, defOp);
  }

  void visit(seq::FirMemOp mem) {
    memOps.push_back(mem);
    auto type = dyn_cast<seq::FirMemType>(mem.getType());
    genArraySort(encodeArraySort(type));
    size_t sid = getSortLID(type);
    size_t opLID = getOpLID((Operation *)mem);
    updateMemView(mem, opLID);
    genArrayState(opLID, sid);
  }

  void genArrayState(size_t opLID, size_t sid) {
    os << opLID << " "
       << "state"
       << " " << sid << " "
       << "\n";
  }

  void visitTypeOp(hw::ArrayCreateOp op) {
    auto type = dyn_cast<hw::ArrayType>(op.getType());
    auto encoding = encodeArraySort(type);
    genArraySort(encoding);
    size_t sid = getSortLID(encoding);
    auto [indexWidth, dataWidth] = encoding;
    size_t arrayLID = lid;
    genArrayState(lid++, sid);
    for (int i = 0, e = type.getNumElements(); i < e; i++) {
      size_t indexLID = lid;
      genConst(i, indexWidth, lid++);
      arrayLID =
          genArrayWrite(lid++, arrayLID, indexLID, op.getOperand(i), encoding);
    }
    opLIDMap[op] = arrayLID;
  }

  void visitTypeOp(hw::AggregateConstantOp op) {
    auto type = dyn_cast<hw::ArrayType>(op.getType());
    auto encoding = encodeArraySort(type);
    genArraySort(encoding);
    size_t sid = getSortLID(encoding);
    auto [indexWidth, dataWidth] = encoding;
    size_t arrayLID = lid;
    genArrayState(lid++, sid);
    int i = 0;
    for (auto dataAttr : op.getFields()) {
      int64_t data = dyn_cast<IntegerAttr>(dataAttr).getValue().getZExtValue();
      size_t indexLID = lid;
      genConst(i, indexWidth, lid++);
      size_t dataLID = lid;
      genConst(data, dataWidth, lid++);
      arrayLID = genArrayWrite(lid++, arrayLID, indexLID, dataLID, encoding);
      i++;
    }
    opLIDMap[op] = arrayLID;
  }

  void visitTypeOp(hw::ArrayGetOp op) {
    size_t dataWidth = requireSort(op.getType());
    genArrayRead(getOpLID((Operation *)op), op.getOperand(0), op.getOperand(1),
                 dataWidth);
  }

  size_t genArrayRead(size_t opLID, Value array, Value index,
                      int64_t dataWidth) {
    return genArrayRead(opLID, getOpLID(array), getOpLID(index), dataWidth);
  }

  size_t genArrayRead(size_t opLID, size_t arrayLID, size_t indexLID,
                      int64_t dataWidth) {
    size_t sid = getSortLID(dataWidth);
    os << opLID << " "
       << "read"
       << " " << sid << " " << arrayLID << " " << indexLID << "\n";
    return opLID;
  }

  size_t genArrayWrite(size_t opLID, size_t arrayLID, Value index, Value data,
                       std::pair<size_t, size_t> encoding) {
    return genArrayWrite(opLID, arrayLID, getOpLID(index), getOpLID(data),
                         encoding);
  }

  size_t genArrayWrite(size_t opLID, size_t arrayLID, size_t indexLID,
                       Value data, std::pair<size_t, size_t> encoding) {
    return genArrayWrite(opLID, arrayLID, indexLID, getOpLID(data), encoding);
  }

  size_t genArrayWrite(size_t opLID, size_t arrayLID, size_t indexLID,
                       size_t dataLID, std::pair<size_t, size_t> encoding) {
    size_t sid = getSortLID(encoding);
    os << opLID << " "
       << "write"
       << " " << sid << " " << arrayLID << " " << indexLID << " " << dataLID
       << "\n";
    return opLID;
  }

  void visitTypeOp(Operation *op) { visitInvalidTypeOp(op); }

  // Handles non-hw operations
  void visitInvalidTypeOp(Operation *op) {
    llvm::TypeSwitch<Operation *, void>(op)
        .Case<seq::FirMemReadOp, seq::FirMemWriteOp, seq::FirMemReadWriteOp>(
            [&](auto expr) { visit(expr); })
        .Default([&](auto expr) { dispatchCombinationalVisitor(op); });
  }

  void visit(seq::FirMemReadOp op) {
    Value mem = op.getMemory();
    auto arrayType = dyn_cast<seq::FirMemType>(mem.getType());
    auto [_, dataWidth] = encodeArraySort(arrayType);
    size_t memLID = getOpLID(mem);
    size_t opLID = getOpLID((Operation *)op);
    genArrayRead(opLID, memLID, getOpLID(op.getAddress()), dataWidth);
  }

  size_t genConcat(size_t leftLID, size_t rightLID, size_t resultWidth) {
    size_t opLID = lid++;
    size_t sid = getSortLID(resultWidth);
    os << opLID << " "
       << "concat"
       << " " << sid << " " << leftLID << " " << rightLID << "\n";
    return opLID;
  }

  size_t extractBit(size_t opLID, size_t bitIndex) {
    size_t bitLID = lid++;
    genSlice(bitLID, opLID, bitIndex, 1);
    return bitLID;
  }

  // Expands a single bit write mask into a full fledge byte mask of `dataWidth`
  // For example, mask = 0x10 will be expanded into 0xff_00
  size_t expandSingleBitMask(size_t maskOpLID, size_t dataWidth,
                             size_t maskWidth) {
    assert(maskWidth == 8);
    size_t onesLID = lid++;
    genConst(0xff, 8, onesLID);
    size_t zerosLID = genZero(8);
    size_t accumLID = noLID;
    size_t currentWidth = 0;
    for (int i = dataWidth / 8 - 1; i >= 0; i--) {
      currentWidth += 8;
      size_t bitLID = extractBit(maskOpLID, i);
      size_t currentByteLID = lid++;
      genIte(currentByteLID, bitLID, onesLID, zerosLID, 8);
      if (accumLID == noLID) {
        accumLID = currentByteLID;
      } else {
        accumLID = genConcat(accumLID, currentByteLID, currentWidth);
      }
    }
    assert(currentWidth == dataWidth);
    return accumLID;
  }

  size_t genMaskGuardedMemWrite(Value mem, Value address, Value data,
                                Value mask, std::optional<uint32_t> maskWidth) {
    auto seqMemType = dyn_cast<seq::FirMemType>(mem.getType());
    auto encoding = encodeArraySort(seqMemType);
    auto [_, dataWidth] = encoding;
    size_t memLID = getArrayStateLID(mem);
    size_t addressLID = getOpLID(address);
    size_t dataLID = getOpLID(data);
    if (maskWidth.has_value()) {
      size_t maskLID = getOpLID(mask);
      size_t maskWidth = mask.getType().getIntOrFloatBitWidth();
      maskLID = expandSingleBitMask(maskLID, dataWidth, maskWidth);
      size_t oldValueLID = genArrayRead(lid++, memLID, addressLID, dataWidth);
      size_t invertedMaskLID = lid++;
      genUnaryOp(invertedMaskLID, maskLID, "not", dataWidth);
      size_t maskedOldLID = lid++;
      genBinOp("and", maskedOldLID, invertedMaskLID, oldValueLID, dataWidth);
      size_t maskedDataSID = lid++;
      genBinOp("and", maskedDataSID, maskLID, dataLID, dataWidth);
      size_t mergedDataSID = lid++;
      genBinOp("or", mergedDataSID, maskedOldLID, maskedDataSID, dataWidth);
      dataLID = mergedDataSID;
    }
    size_t opLID = lid++;
    genArrayWrite(opLID, memLID, addressLID, dataLID, encoding);
    return opLID;
  }

  void visit(seq::FirMemWriteOp op) {
    auto mem = op.getMemory();
    size_t opLID = genMaskGuardedMemWrite(
        mem, op.getAddress(), op.getData(), op.getMask(),
        dyn_cast<seq::FirMemType>(mem.getType()).getMaskWidth());
    updateMemView(mem, opLID);
  }

  void visit(seq::FirMemReadWriteOp op) {
    Value mem = op.getMemory(), address = op.getAddress(),
          data = op.getWriteData(), mask = op.getMask();
    auto memType = dyn_cast<seq::FirMemType>(mem.getType());
    auto encoding = encodeArraySort(memType);
    auto [_, dataWidth] = encoding;
    size_t lastViewLID = getOpLID(mem);
    size_t writtenMemLID = genMaskGuardedMemWrite(mem, address, data, mask,
                                                  memType.getMaskWidth());
    size_t readLID =
        genArrayRead(lid++, lastViewLID, getOpLID(address), dataWidth);

    size_t modeLID = getOpLID(op.getMode());
    genIte(op, modeLID, getOpLID(data), readLID, dataWidth);
    size_t updatedMemLID = lid++;
    genIte(updatedMemLID, modeLID, writtenMemLID, lastViewLID, encoding);
    updateMemView(mem, updatedMemLID);
  }

  // Binary operations are all emitted the same way, so we can group them into
  // a single method.
  void visitBinOp(Operation *op, StringRef inst, int64_t w) {

    // Start by extracting the operands
    Value op1 = op->getOperand(0);
    Value op2 = op->getOperand(1);

    // Generate the line
    genBinOp(inst, op, op1, op2, w);
  }

  // Expands a variadic operation into multiple binary operation instructions
  void genVariadicOp(StringRef inst, Operation *op, size_t width) {
    auto operands = op->getOperands();
    size_t sid = getSortLID(width);

    if (operands.size() == 0) {
      op->emitError("variadic operations with no operands are not supported");
      return;
    }

    // If there's only one operand, then we don't generate a BTOR2 instruction,
    // we just reuse the operand's existing LID
    if (operands.size() == 1) {
      auto existingLID = getOpLID(operands[0]);
      // Check that we haven't somehow got a value that doesn't have a
      // corresponding LID
      assert(existingLID != noLID);
      opLIDMap[op] = existingLID;
      return;
    }

    // Special case for concat since intermediate results need different sorts
    auto isConcat = isa<comb::ConcatOp>(op);

    // Unroll variadic op into series of binary ops
    // This will represent the previous operand in the chain:
    auto prevOperandLID = getOpLID(operands[0]);

    // Track the current width so we can work out new types if this is a concat
    auto currentWidth = operands[0].getType().getIntOrFloatBitWidth();

    for (auto operand : operands.drop_front()) {
      // Manually increment lid since we need multiple per op

      if (isConcat) {
        // For concat, the sort width increases with each operand
        currentWidth += operand.getType().getIntOrFloatBitWidth();
        // Ensure that the sort exists
        genSort("bitvec", currentWidth);
      }

      auto thisLid = lid++;
      auto thisOperandLID = getOpLID(operand);
      os << thisLid << " " << inst << " "
         << (isConcat ? getSortLID(currentWidth) : sid) << " " << prevOperandLID
         << " " << thisOperandLID << "\n";
      prevOperandLID = thisLid;
    }

    // Send lookups of the op's LID to the final binary op in the chain
    opLIDMap[op] = prevOperandLID;
  }

  template <typename Op> void visitVariadicOp(Op op, StringRef inst) {
    // Generate the sort
    int64_t w = requireSort(op.getType());

    // Generate the line
    genVariadicOp(inst, op, w);
  }
  // Binary operations are all emitted the same way, so we can group them into
  // a single method.
  template <typename Op> void visitBinOp(Op op, StringRef inst) {
    // Generate the sort
    int64_t w = requireSort(op.getType());

    // Start by extracting the operands
    Value op1 = op.getOperand(0);
    Value op2 = op.getOperand(1);

    // Generate the line
    genBinOp(inst, op, op1, op2, w);
  }

  // Visitors for the binary ops
  void visitComb(comb::AddOp op) { visitVariadicOp(op, "add"); }
  void visitComb(comb::SubOp op) { visitBinOp(op, "sub"); }
  void visitComb(comb::MulOp op) { visitVariadicOp(op, "mul"); }
  void visitComb(comb::DivSOp op) { visitBinOp(op, "sdiv"); }
  void visitComb(comb::DivUOp op) { visitBinOp(op, "udiv"); }
  void visitComb(comb::ModSOp op) { visitBinOp(op, "smod"); }
  void visitComb(comb::ShlOp op) { visitBinOp(op, "sll"); }
  void visitComb(comb::ShrUOp op) { visitBinOp(op, "srl"); }
  void visitComb(comb::ShrSOp op) { visitBinOp(op, "sra"); }
  void visitComb(comb::AndOp op) { visitVariadicOp(op, "and"); }
  void visitComb(comb::OrOp op) { visitVariadicOp(op, "or"); }
  void visitComb(comb::XorOp op) { visitVariadicOp(op, "xor"); }
  void visitComb(comb::ConcatOp op) { visitVariadicOp(op, "concat"); }

  // Extract ops translate to a slice operation in btor2 in a one-to-one manner
  void visitComb(comb::ExtractOp op) {
    int64_t w = requireSort(op.getType());

    // Start by extracting the necessary information for the emission (i.e.
    // operand, low bit, ...)
    Value op0 = op.getOperand();
    size_t lb = op.getLowBit();

    // Generate the slice instruction
    genSlice(op, op0, lb, w);
  }

  // Btor2 uses similar syntax as hw for its comparisons
  // So we simply need to emit the cmpop name and check for corner cases
  // where the namings differ.
  void visitComb(comb::ICmpOp op) {
    Value lhs = op.getOperand(0);
    Value rhs = op.getOperand(1);

    // Extract the predicate name (assuming that its a valid btor2
    // predicate)
    StringRef pred = stringifyICmpPredicate(op.getPredicate());

    // Check for special cases where hw doesn't align with btor syntax
    if (pred == "ne")
      pred = "neq";
    else if (pred == "ule")
      pred = "ulte";
    else if (pred == "sle")
      pred = "slte";
    else if (pred == "uge")
      pred = "ugte";
    else if (pred == "sge")
      pred = "sgte";

    // Width of result is always 1 for comparison
    genSort("bitvec", 1);

    // With the special cases out of the way, the emission is the same as that
    // of a binary op
    genBinOp(pred, op, lhs, rhs, 1);
  }

  // Muxes generally convert to an ite statement
  void visitComb(comb::MuxOp op) {
    // Extract predicate, true and false values
    Value pred = op.getCond();
    Value tval = op.getTrueValue();
    Value fval = op.getFalseValue();

    // We assume that both tval and fval have the same width
    // This width should be the same as the output width
    int64_t w = requireSort(op.getType());

    // Generate the ite instruction
    genIte(op, pred, tval, fval, w);
  }

  void visitComb(comb::ReplicateOp op) {
    Value op0 = op.getOperand();
    auto count = op.getMultiple();
    auto inputWidth = op0.getType().getIntOrFloatBitWidth();

    // Generate the concat chain
    size_t opLID = genReplicateAsConcats(getOpLID(op0), count, inputWidth);
    opLIDMap[(Operation *)op] = opLID;
  }

  size_t genReplicateAsConcats(size_t op0LID, size_t count,
                               unsigned int inputWidth) {
    auto currentWidth = inputWidth;

    auto prevOperandLID = op0LID;
    for (size_t i = 1; i < count; ++i) {
      currentWidth += inputWidth;
      // Ensure that the sort exists
      genSort("bitvec", currentWidth);

      auto thisLid = lid++;
      os << thisLid << " "
         << "concat"
         << " " << getSortLID(currentWidth) << " " << prevOperandLID << " "
         << op0LID << "\n";
      prevOperandLID = thisLid;
    }
    return prevOperandLID;
  }

  void visitComb(Operation *op) { visitInvalidComb(op); }

  // Try sv ops when comb is done
  void visitInvalidComb(Operation *op) { dispatchSVVisitor(op); }

  // Assertions are negated then converted to a btor2 bad instruction
  void visitSV(sv::AssertOp op) {
    // Expression is what we will try to invert for our assertion
    Value expr = op.getExpression();

    // This sort is for assertion inversion and potential implies
    genSort("bitvec", 1);

    // Check for an overaching enable
    // In our case the sv.if operation will probably only be used when
    // conditioning an sv.assert on an enable signal. This means that
    // its condition is probably used to imply our assertion
    if (auto ifop = dyn_cast<sv::IfOp>(((Operation *)op)->getParentOp())) {
      Value en = ifop.getOperand();

      // Generate the implication
      genImplies(ifop, en, expr);

      // Generate the implies inversion
      genUnaryOp(op, ifop, "not", 1);
    } else {
      // Generate the expression inversion
      genUnaryOp(op, expr, "not", 1);
    }

    // Genrate the bad btor2 intruction
    genBad(op);
  }
  // Assumptions are converted to a btor2 constraint instruction
  void visitSV(sv::AssumeOp op) {
    // Extract the expression that we want our constraint to be about
    Value expr = op.getExpression();
    genConstraint(expr);
  }

  void visitSV(Operation *op) { visitInvalidSV(op); }

  // Once SV Ops are visited, we need to check for seq ops
  void visitInvalidSV(Operation *op) { visit(op); }

  // Seq operation visitor, that dispatches to other seq ops
  // Also handles all remaining operations that should be explicitly ignored
  void visit(Operation *op) {
    // Typeswitch is used here because other seq types will be supported
    // like all operations relating to memories and CompRegs
    TypeSwitch<Operation *, void>(op)
        .Case<seq::FirRegOp, hw::WireOp, hw::OutputOp>(
            [&](auto expr) { visit(expr); })
        .Default([&](auto expr) { visitUnsupportedOp(op); });
  }

  // Firrtl registers generate a state instruction
  // The final update is also used to generate a set of next btor
  // instructions
  void visit(seq::FirRegOp reg) {
    // Start by retrieving the register's name and width
    StringRef regName = reg.getName();
    int64_t w = requireSort(reg.getType());

    // Generate state instruction (represents the register declaration)
    genState(reg, w, regName);

    // Record the operation for future `next` instruction generation
    // This is required to model transitions between states (i.e. how a
    // register's value evolves over time)
    regOps.push_back(reg);
  }

  // Compregs behave in a similar way as firregs for btor2 emission
  void visit(seq::CompRegOp reg) {
    // Start by retrieving the register's name and width
    StringRef regName = reg.getName().value();
    int64_t w = requireSort(reg.getType());

    // Check for initial values which must be emitted before the state in btor2
    Value pov = reg.getPowerOnValue();
    if (pov) {
      // Check that the powerOn value is a non-null constant
      if (!isa_and_nonnull<hw::ConstantOp>(pov.getDefiningOp()))
        reg->emitError("PowerOn Value must be constant!!");

      // Visit the powerOn Value to generate the constant
      dispatchTypeOpVisitor(pov.getDefiningOp());

      // Add it to the list of visited operations
      handledOps.insert(pov.getDefiningOp());

      // Generate state instruction (represents the register declaration)
      genState(reg, w, regName);

      // Finally generate the init statement
      genInit(reg, pov, w);

    } else {
      // Only generate the state instruction and nothing else
      genState(reg, w, regName);
    }

    // Record the operation for future `next` instruction generation
    // This is required to model transitions between states (i.e. how a
    // register's value evolves over time)
    regOps.push_back(reg);
  }

  // Ignore all other explicitly mentionned operations
  // ** Purposefully left empty **
  void ignore(Operation *op) {}

  // Tail method that handles all operations that weren't handled by previous
  // visitors. Here we simply make the pass fail or ignore the op
  void visitUnsupportedOp(Operation *op) {
    // Check for explicitly ignored ops vs unsupported ops (which cause a
    // failure)
    TypeSwitch<Operation *, void>(op)
        // All explicitly ignored operations are defined here
        .Case<sv::MacroRefExprOp, sv::MacroDefOp, sv::ErrorOp, sv::FatalOp,
              sv::MacroDeclOp, sv::VerbatimOp, sv::VerbatimExprOp,
              sv::VerbatimExprSEOp, sv::IfOp, sv::IfDefOp,
              sv::IfDefProceduralOp, sv::AlwaysOp, sv::AlwaysCombOp,
              sv::FWriteOp, sv::AlwaysFFOp, seq::FromClockOp, seq::ToClockOp,
              seq::ConstClockOp, hw::HWModuleOp>([&](auto expr) { ignore(op); })

        // Make sure that the design only contains one clock
        .Case<seq::FromClockOp>([&](auto expr) {
          if (++nclocks > 1UL) {
            op->emitOpError("Mutli-clock designs are not supported!");
            return signalPassFailure();
          }
        })

        // Anything else is considered unsupported and might cause a wrong
        // behavior if ignored, so an error is thrown
        .Default([&](auto expr) {
          op->emitOpError("is an unsupported operation");
          return signalPassFailure();
        });
  }
};
} // end anonymous namespace

void ConvertHWToBTOR2Pass::runOnOperation() {
  // Btor2 does not have the concept of modules or module
  // hierarchies, so we assume that no nested modules exist at this point.
  // This greatly simplifies translation.
  getOperation().walk([&](hw::HWModuleOp module) {
    // Start by extracting the inputs and generating appropriate instructions
    for (auto &port : module.getPortList()) {
      if (port.isOutput()) {
        outputPorts.push_back(port);
      }
      visit(port);
    }

    // Previsit all registers in the module in order to avoid dependency cylcles
    module.walk([&](Operation *op) {
      TypeSwitch<Operation *, void>(op)
          .Case<seq::FirRegOp, seq::CompRegOp, seq::FirMemOp>([&](auto reg) {
            visit(reg);
            handledOps.insert(op);
          })
          .Default([&](auto expr) {});
    });

    // Visit all of the operations in our module
    module.walk([&](Operation *op) {
      // Check: instances are not (yet) supported
      if (isa<hw::InstanceOp>(op)) {
        op->emitOpError("not supported in BTOR2 conversion");
        return;
      }

      // Don't process ops that have already been emitted
      if (handledOps.contains(op))
        return;

      // Fill in our worklist
      worklist.insert({op, op->operand_begin()});

      // Process the elements in our worklist
      while (!worklist.empty()) {
        auto &[op, operandIt] = worklist.back();
        if (operandIt == op->operand_end()) {
          // All of the operands have been emitted, it is safe to emit our op
          dispatchTypeOpVisitor(op);

          // Record that our op has been emitted
          handledOps.insert(op);
          worklist.pop_back();
          continue;
        }

        // Send the operands of our op to the worklist in case they are still
        // un-emitted
        Value operand = *(operandIt++);
        auto *defOp = operand.getDefiningOp();

        // Make sure that we don't emit the same operand twice
        if (!defOp || handledOps.contains(defOp))
          continue;

        // This is triggered if our operand is already in the worklist and
        // wasn't handled
        if (!worklist.insert({defOp, defOp->operand_begin()}).second) {
          op->emitError("dependency cycle");
          return;
        }
      }
    });

    for (size_t i = 0; i < memOps.size(); ++i) {
      finalizeArrayUpdate(memOps[i]);
    }

    // Iterate through the registers and generate the `next` instructions
    for (size_t i = 0; i < regOps.size(); ++i) {
      finalizeRegVisit(regOps[i]);
    }
  });
  // Clear data structures to allow for pass reuse
  sortToLIDMap.clear();
  constToLIDMap.clear();
  opLIDMap.clear();
  opAliasMap.clear();
  inputLIDs.clear();
  regOps.clear();
  handledOps.clear();
  worklist.clear();
}

// Constructor with a custom ostream
std::unique_ptr<mlir::Pass>
circt::createConvertHWToBTOR2Pass(llvm::raw_ostream &os) {
  return std::make_unique<ConvertHWToBTOR2Pass>(os);
}

// Basic default constructor
std::unique_ptr<mlir::Pass> circt::createConvertHWToBTOR2Pass() {
  return std::make_unique<ConvertHWToBTOR2Pass>(llvm::outs());
}
