#include "hir_typeck_helpers.h"

#include <std/mem/obj_pool.h>
#include <std/tst/ut.h>

using namespace stl;

STD_TEST_SUITE(HMTypeInferrenceSnapshot) {
    STD_TEST(testRollbackRestoresBinding) {
        auto pool = stl::ObjPool::fromMemory();
        u32 id = 0;
        HIRTypeInterner types(*pool.mutPtr(), id);
        HMTypeInferrence table(types);

        const auto a = table.newIvar();
        const auto genBefore = table.mutationGeneration;

        auto snap = table.snapshot();
        table.setIvarTo(a, types.primitive(HIRCoreType::I32));
        STD_INSIST(table.getType(a)->is_Primitive());
        const auto genInside = table.mutationGeneration;
        STD_INSIST(genInside != genBefore);
        table.rollbackTo(snap);

        STD_INSIST(table.getType(a)->is_Infer());
        STD_INSIST(table.mutationGeneration == genBefore);
        STD_INSIST(!table.probing());

        // A rolled-back generation never recurs.
        table.setIvarTo(a, types.primitive(HIRCoreType::U8));
        STD_INSIST(table.mutationGeneration != genInside);
        STD_INSIST(table.mutationGeneration != genBefore);
    }

    STD_TEST(testRollbackUndoesAliasAndTruncates) {
        auto pool = stl::ObjPool::fromMemory();
        u32 id = 0;
        HIRTypeInterner types(*pool.mutPtr(), id);
        HMTypeInferrence table(types);

        const auto a = table.newIvar();
        const auto b = table.newIvar();
        const auto sizeBefore = table.ivars.size();

        auto snap = table.snapshot();
        const auto generationBeforeTemporary = table.mutationGeneration;
        const auto c = table.newIvar();
        const auto temporaryGeneration = table.mutationGeneration;
        STD_INSIST(temporaryGeneration != generationBeforeTemporary);
        // Alias b to a, then bind a through a fresh probe variable chain.
        table.setIvarTo(a, types.infer(b));
        table.setIvarTo(c, types.primitive(HIRCoreType::I32));
        STD_INSIST(table.ivars.at(b).isAlias());
        table.rollbackTo(snap);

        STD_INSIST(table.ivars.size() == sizeBefore);
        STD_INSIST(!table.ivars.at(a).isAlias());
        STD_INSIST(!table.ivars.at(b).isAlias());
        STD_INSIST(table.getType(a)->is_Infer());
        STD_INSIST(table.getType(b)->is_Infer());

        const auto reused = table.newIvar();
        STD_INSIST(reused == c);
        STD_INSIST(table.mutationGeneration != temporaryGeneration);
    }

    STD_TEST(testRollbackRestoresLiteralClassUpgrade) {
        auto pool = stl::ObjPool::fromMemory();
        u32 id = 0;
        HIRTypeInterner types(*pool.mutPtr(), id);
        HMTypeInferrence table(types);

        const auto l = table.newIvar();
        const auto r = table.newIvar(HIRInferClass::Integer);

        auto snap = table.snapshot();
        table.ivarUnify(l, r);
        STD_INSIST(table.getType(l)->as_Infer().tyClass == HIRInferClass::Integer);
        STD_INSIST(table.ivars.at(r).isAlias());
        table.rollbackTo(snap);

        STD_INSIST(table.getType(l)->as_Infer().tyClass == HIRInferClass::None);
        STD_INSIST(!table.ivars.at(r).isAlias());
        STD_INSIST(table.getType(r)->as_Infer().tyClass == HIRInferClass::Integer);
    }

    STD_TEST(testRollbackRestoresValueIvars) {
        auto pool = stl::ObjPool::fromMemory();
        u32 id = 0;
        HIRTypeInterner types(*pool.mutPtr(), id);
        HMTypeInferrence table(types);

        const auto v1 = table.newIvarVal();
        const auto v2 = table.newIvarVal();

        auto snap = table.snapshot();
        table.setIvarValTo(v1, HIRConstGeneric(HIRGenericRef(RcString::newInterned("N"), 0)));
        table.ivarValUnify(v1, v2);
        STD_INSIST(!table.getValue(v1).is_Infer());
        STD_INSIST(table.values.at(v2).isAlias());
        table.rollbackTo(snap);

        STD_INSIST(table.getValue(v1).is_Infer());
        STD_INSIST(table.getValue(v1).as_Infer().index == v1);
        STD_INSIST(!table.values.at(v2).isAlias());
        STD_INSIST(table.getValue(v2).is_Infer());
        STD_INSIST(table.getValue(v2).as_Infer().index == v2);
    }

    STD_TEST(testCommitKeepsBindings) {
        auto pool = stl::ObjPool::fromMemory();
        u32 id = 0;
        HIRTypeInterner types(*pool.mutPtr(), id);
        HMTypeInferrence table(types);

        const auto a = table.newIvar();
        const auto v = table.newIvarVal();

        auto snap = table.snapshot();
        table.setIvarTo(a, types.primitive(HIRCoreType::I32));
        table.setIvarValTo(v, HIRConstGeneric(HIRGenericRef(RcString::newInterned("N"), 0)));
        table.commit(snap);

        STD_INSIST(table.getType(a)->is_Primitive());
        STD_INSIST(!table.getValue(v).is_Infer());
        STD_INSIST(!table.probing());

        // A later probe must not be able to undo the committed state.
        const auto b = table.newIvar();
        auto snap2 = table.snapshot();
        table.setIvarTo(b, types.primitive(HIRCoreType::U8));
        table.rollbackTo(snap2);
        STD_INSIST(table.getType(a)->is_Primitive());
        STD_INSIST(table.getType(b)->is_Infer());
    }

    STD_TEST(testNestedSnapshots) {
        auto pool = stl::ObjPool::fromMemory();
        u32 id = 0;
        HIRTypeInterner types(*pool.mutPtr(), id);
        HMTypeInferrence table(types);

        const auto a = table.newIvar();
        const auto b = table.newIvar();

        auto outer = table.snapshot();
        table.setIvarTo(a, types.primitive(HIRCoreType::I32));
        auto inner = table.snapshot();
        table.setIvarTo(b, types.primitive(HIRCoreType::U8));
        table.rollbackTo(inner);
        STD_INSIST(table.getType(a)->is_Primitive());
        STD_INSIST(table.getType(b)->is_Infer());
        STD_INSIST(table.probing());
        table.rollbackTo(outer);
        STD_INSIST(table.getType(a)->is_Infer());
        STD_INSIST(table.getType(b)->is_Infer());
        STD_INSIST(!table.probing());
    }

    STD_TEST(testUnifyBindsAndUnifies) {
        auto pool = stl::ObjPool::fromMemory();
        u32 id = 0;
        HIRTypeInterner types(*pool.mutPtr(), id);
        HMTypeInferrence table(types);
        Span sp;

        const auto a = table.newIvar();
        const auto b = table.newIvar();
        Unifier unifier(sp, table);

        // ?a := (i32, ?b), then ?b := u8: both propagate through the table.
        const auto pairTy = types.tuple({types.primitive(HIRCoreType::I32), types.infer(b)});
        STD_INSIST(unifier.unify(types.infer(a), pairTy) == Unifier::Outcome::Unified);
        STD_INSIST(unifier.unify(types.infer(b), types.primitive(HIRCoreType::U8)) == Unifier::Outcome::Unified);
        STD_INSIST(table.getType(b)->is_Primitive());
        STD_INSIST(unifier.pending().length() == 0);
    }

    STD_TEST(testUnifyMismatchRollsBack) {
        auto pool = stl::ObjPool::fromMemory();
        u32 id = 0;
        HIRTypeInterner types(*pool.mutPtr(), id);
        HMTypeInferrence table(types);
        Span sp;

        const auto a = table.newIvar();
        Unifier unifier(sp, table);

        // (?a, i32) vs (u8, u16): ?a binds underway, then the mismatch on
        // the second element must roll that binding back.
        const auto leftTy = types.tuple({types.infer(a), types.primitive(HIRCoreType::I32)});
        const auto rightTy = types.tuple({types.primitive(HIRCoreType::U8), types.primitive(HIRCoreType::U16)});
        STD_INSIST(unifier.unify(leftTy, rightTy) == Unifier::Outcome::Mismatch);
        STD_INSIST(table.getType(a)->is_Infer());
    }

    STD_TEST(testUnifyOccursCheck) {
        auto pool = stl::ObjPool::fromMemory();
        u32 id = 0;
        HIRTypeInterner types(*pool.mutPtr(), id);
        HMTypeInferrence table(types);
        Span sp;

        const auto a = table.newIvar();
        Unifier unifier(sp, table);

        const auto recursive = types.tuple({types.infer(a)});
        STD_INSIST(unifier.unify(types.infer(a), recursive) == Unifier::Outcome::Mismatch);
        STD_INSIST(table.getType(a)->is_Infer());
    }

    STD_TEST(testUnifyLiteralClasses) {
        auto pool = stl::ObjPool::fromMemory();
        u32 id = 0;
        HIRTypeInterner types(*pool.mutPtr(), id);
        HMTypeInferrence table(types);
        Span sp;

        const auto lit = table.newIvar(HIRInferClass::Integer);
        const auto fl = table.newIvar(HIRInferClass::Float);
        Unifier unifier(sp, table);

        STD_INSIST(unifier.unify(types.infer(lit), types.primitive(HIRCoreType::F64)) == Unifier::Outcome::Mismatch);
        STD_INSIST(unifier.unify(types.infer(lit), types.infer(fl)) == Unifier::Outcome::Mismatch);
        STD_INSIST(unifier.unify(types.infer(lit), types.primitive(HIRCoreType::U32)) == Unifier::Outcome::Unified);
        STD_INSIST(table.getType(lit)->is_Primitive());
    }

    STD_TEST(testUnifyDefersRigidUnknowns) {
        auto pool = stl::ObjPool::fromMemory();
        u32 id = 0;
        HIRTypeInterner types(*pool.mutPtr(), id);
        HMTypeInferrence table(types);
        Span sp;

        Unifier unifier(sp, table);

        // A match placeholder is a rigid unknown: the equality is neither
        // proven nor refuted, it is collected as data.
        const auto placeholder = types.generic(RcString::newInterned("impl_?_test"), GENERICPlaceholder << 8);
        STD_INSIST(unifier.unify(placeholder, types.primitive(HIRCoreType::I32)) == Unifier::Outcome::Unified);
        STD_INSIST(unifier.pending().length() == 1);
        STD_INSIST(unifier.pending()[0].right->is_Primitive() || unifier.pending()[0].left->is_Primitive());

        // A solver-canonical variable stays rigid too.
        const auto canonical = types.infer(HIR_INFER_SOLVER_CANONICAL_MIN);
        STD_INSIST(unifier.unify(canonical, types.primitive(HIRCoreType::U8)) == Unifier::Outcome::Unified);
        STD_INSIST(unifier.pending().length() == 2);

        // Distinct rigid generics can never be equal.
        const auto genericT = types.generic(RcString::newInterned("T"), 0);
        const auto genericU = types.generic(RcString::newInterned("U"), 1);
        STD_INSIST(unifier.unify(genericT, genericU) == Unifier::Outcome::Mismatch);
        STD_INSIST(unifier.pending().length() == 2);
    }

    STD_TEST(testUnifyBindsExistentialToCanonicalInput) {
        auto pool = stl::ObjPool::fromMemory();
        u32 id = 0;
        HIRTypeInterner types(*pool.mutPtr(), id);
        HMTypeInferrence table(types);
        Span sp;

        const auto existential = table.newIvar();
        const auto canonical = types.infer(HIR_INFER_SOLVER_CANONICAL_MIN);
        Unifier unifier(sp, table);

        STD_INSIST(unifier.unify(types.infer(existential), canonical) == Unifier::Outcome::Unified);
        STD_INSIST(table.getType(types.infer(existential)) == canonical);
        STD_INSIST(unifier.pending().length() == 0);
    }

    STD_TEST(testCanonicalLiteralSlotRejectsStructuralType) {
        auto pool = stl::ObjPool::fromMemory();
        u32 id = 0;
        HIRTypeInterner types(*pool.mutPtr(), id);
        HMTypeInferrence table(types);
        Span sp;

        const auto canonicalInteger = types.infer(HIR_INFER_SOLVER_CANONICAL_MIN, HIRInferClass::Integer);
        Unifier unifier(sp, table);

        STD_INSIST(unifier.unify(canonicalInteger, types.borrow(HIRBorrowType::Shared, types.primitive(HIRCoreType::Usize))) == Unifier::Outcome::Mismatch);
        STD_INSIST(unifier.pending().length() == 0);
        STD_INSIST(unifier.unify(canonicalInteger, types.diverge()) == Unifier::Outcome::Mismatch);
        STD_INSIST(unifier.pending().length() == 0);
        STD_INSIST(unifier.unify(canonicalInteger, types.primitive(HIRCoreType::Usize)) == Unifier::Outcome::Unified);
        STD_INSIST(unifier.pending().length() == 1);
    }

    STD_TEST(testUnifyArrayBindsConstLength) {
        auto pool = stl::ObjPool::fromMemory();
        u32 id = 0;
        HIRTypeInterner types(*pool.mutPtr(), id);
        HMTypeInferrence table(types);
        Span sp;

        const auto length = table.newIvarVal();
        const auto element = types.primitive(HIRCoreType::U8);
        const auto genericArray = types.array(element, HIRConstGeneric::make_Infer({length}));
        const auto knownArray = types.array(element, 2);
        Unifier unifier(sp, table);

        STD_INSIST(unifier.unify(genericArray, knownArray) == Unifier::Outcome::Unified);
        const auto& resolved = table.getValue(HIRConstGeneric::make_Infer({length}));
        STD_INSIST(resolved.is_Evaluated());
        STD_INSIST(resolved.as_Evaluated()->readUsize(0) == 2);
        STD_INSIST(unifier.pendingValues().empty());
    }

    STD_TEST(testCandidateConstExistentialCapturesRigidPlaceholder) {
        auto pool = stl::ObjPool::fromMemory();
        u32 id = 0;
        HIRTypeInterner types(*pool.mutPtr(), id);
        HMTypeInferrence table(types);
        Span sp;

        const auto ordinarySlot = table.newIvarVal();
        const auto candidateSlot = table.newIvarVal();
        const auto placeholder = HIRConstGeneric(HIRGenericRef(RcString::newInterned("const_?_test"), GENERICPlaceholder << 8));

        Unifier ordinary(sp, table);
        STD_INSIST(ordinary.unifyValues(placeholder, HIRConstGeneric::make_Infer({ordinarySlot})) == Unifier::Outcome::Unified);
        STD_INSIST(ordinary.pendingValues().size() == 1);
        STD_INSIST(table.getValue(ordinarySlot).is_Infer());

        Unifier candidate(sp, table, nullptr, true);
        STD_INSIST(candidate.unifyValues(placeholder, HIRConstGeneric::make_Infer({candidateSlot})) == Unifier::Outcome::Unified);
        STD_INSIST(candidate.pendingValues().empty());
        STD_INSIST(table.getValue(candidateSlot) == placeholder);
    }

    STD_TEST(testRollbackRestoresChangedFlag) {
        auto pool = stl::ObjPool::fromMemory();
        u32 id = 0;
        HIRTypeInterner types(*pool.mutPtr(), id);
        HMTypeInferrence table(types);

        const auto a = table.newIvar();
        (void)table.takeChanged();

        auto snap = table.snapshot();
        table.setIvarTo(a, types.primitive(HIRCoreType::I32));
        STD_INSIST(table.peekChanged());
        table.rollbackTo(snap);
        STD_INSIST(!table.peekChanged());
    }
}
