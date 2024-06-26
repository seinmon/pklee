//===-- PTree.h -------------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_PTREE_H
#define KLEE_PTREE_H

#include "PTreeWriter.h"
#include "UserSearcher.h"
#include "klee/Core/BranchTypes.h"
#include "klee/Core/TerminationTypes.h"
#include "klee/Expr/Expr.h"
#include "klee/Support/ErrorHandling.h"

#include "llvm/ADT/PointerIntPair.h"
#include "llvm/Support/Casting.h"

#include <variant>

namespace klee {
class ExecutionState;
class Executor;
class InMemoryPTree;
class InterpreterHandler;
class PTreeNode;
class Searcher;

/* PTreeNodePtr is used by the Random Path Searcher object to efficiently
record which PTreeNode belongs to it. PTree is a global structure that
captures all  states, whereas a Random Path Searcher might only care about
a subset. The integer part of PTreeNodePtr is a bitmask (a "tag") of which
Random Path Searchers PTreeNode belongs to. */
constexpr std::uint8_t PtrBitCount = 3;
using PTreeNodePtr = llvm::PointerIntPair<PTreeNode *, PtrBitCount, uint8_t>;

class PTreeNode {
public:
  enum NodeType { NT_Basic, NT_Annotated };

  PTreeNode *parent{nullptr};
  PTreeNodePtr left;
  PTreeNodePtr right;
  ExecutionState *state{nullptr};

  PTreeNode(PTreeNode *parent, ExecutionState *state) noexcept;
  virtual ~PTreeNode() = default;
  PTreeNode(const PTreeNode &) = delete;
  PTreeNode &operator=(PTreeNode const &) = delete;
  PTreeNode(PTreeNode &&) = delete;
  PTreeNode &operator=(PTreeNode &&) = delete;

  [[nodiscard]] NodeType getType() const { return nodeType; }
  static bool classof(const PTreeNode *N) { return true; }

private:
  NodeType nodeType;

protected:
  PTreeNode(PTreeNode *parent, ExecutionState *state, NodeType nt) noexcept;
};

class AnnotatedPTreeNode : public PTreeNode {
  inline static std::uint32_t nextID{1};

public:
  std::uint32_t id{0};
  std::uint32_t stateID{0};
  std::uint32_t asmLine{0};
  std::variant<BranchType, StateTerminationType> kind{BranchType::NONE};

  AnnotatedPTreeNode(PTreeNode *parent, ExecutionState *state) noexcept;
  ~AnnotatedPTreeNode() override = default;

  static bool classof(const PTreeNode *N) {
    return N->getType() == NT_Annotated;
  }
};

class PTree {
public:
  enum PTreeType { PT_Basic, PT_Noop, PT_InMemory, PT_Persistent };

  /// Branch from PTreeNode and attach states, convention: rightState is
  /// parent
  virtual void attach(PTreeNode *node, ExecutionState *leftState,
                      ExecutionState *rightState, BranchType reason) = 0;
  /// Dump process tree in .dot format into os (debug)
  virtual void dump(llvm::raw_ostream &os) = 0;
  /// Remove node from tree
  virtual void remove(PTreeNode *node) = 0;
  /// Set termination type (on state removal)
  virtual void setTerminationType(ExecutionState &state,
                                  StateTerminationType type){};

  virtual ~PTree() = default;
  PTree(PTree const &) = delete;
  PTree &operator=(PTree const &) = delete;
  PTree(PTree &&) = delete;
  PTree &operator=(PTree &&) = delete;

  [[nodiscard]] PTreeType getType() const { return ptreeType; }
  static bool classof(const PTreeNode *N) { return true; }

protected:
  explicit PTree(PTreeType pt) noexcept : ptreeType{pt} {};

private:
  PTreeType ptreeType;
};

/// @brief A pseudo process tree that does not maintain any nodes.
class NoopPTree final : public PTree {
public:
  NoopPTree() noexcept;
  ~NoopPTree() override = default;
  void attach(PTreeNode *node, ExecutionState *leftState,
              ExecutionState *rightState,
              BranchType reason) noexcept override{};
  void dump(llvm::raw_ostream &os) noexcept override;
  void remove(PTreeNode *node) noexcept override{};

  static bool classof(const PTree *T) { return T->getType() == PT_Noop; }
};

/// @brief An in-memory process tree required by RandomPathSearcher
class InMemoryPTree : public PTree {
public:
  PTreeNodePtr root;

private:
  /// Number of registered IDs ("users", e.g. RandomPathSearcher)
  std::uint8_t registeredIds = 0;

  virtual PTreeNode *createNode(PTreeNode *parent, ExecutionState *state);
  virtual void updateBranchingNode(PTreeNode &node, BranchType reason){};
  virtual void updateTerminatingNode(PTreeNode &node){};

public:
  explicit InMemoryPTree(ExecutionState &initialState) noexcept;
  ~InMemoryPTree() override = default;

  void attach(PTreeNode *node, ExecutionState *leftState,
              ExecutionState *rightState, BranchType reason) noexcept override;
  void dump(llvm::raw_ostream &os) noexcept override;
  std::uint8_t getNextId() noexcept;
  void remove(PTreeNode *node) noexcept override;

  static bool classof(const PTree *T) {
    return (T->getType() == PT_InMemory) || (T->getType() == PT_Persistent);
  }

protected:
  InMemoryPTree(ExecutionState &initialState, PTreeType pt) noexcept;
};

/// @brief An in-memory process tree that also writes its nodes into an SQLite
/// database (ptree.db) with a PTreeWriter
class PersistentPTree : public InMemoryPTree {
  PTreeWriter writer;

  PTreeNode *createNode(PTreeNode *parent, ExecutionState *state) override;
  void updateBranchingNode(PTreeNode &node, BranchType reason) override;
  void updateTerminatingNode(PTreeNode &node) override;

public:
  explicit PersistentPTree(ExecutionState &initialState,
                           InterpreterHandler &ih) noexcept;
  void dump(llvm::raw_ostream &os) noexcept override;
  void setTerminationType(ExecutionState &state,
                          StateTerminationType type) override;

  static bool classof(const PTree *T) { return T->getType() == PT_Persistent; }
};

std::unique_ptr<PTree> getPTree(ExecutionState &state, bool inMemory,
                                InterpreterHandler &ih);
} // namespace klee

#endif /* KLEE_PTREE_H */
