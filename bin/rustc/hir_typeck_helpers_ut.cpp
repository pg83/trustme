#include "hir_typeck_expr_cs.h"
#include "hir_typeck_helpers.h"

#include <std/tst/ut.h>
#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    [[maybe_unused]] void solverResponseApiGate(SolverResponse& response) {
        auto& [certainty, slots, obligations, equalities, valueEqualities, impl, operatorSummary] = response;
        (void)certainty;
        (void)slots;
        (void)obligations;
        (void)equalities;
        (void)valueEqualities;
        (void)impl;
        (void)operatorSummary;
    }

    [[maybe_unused]] void traitGoalQueryApiGate(TraitGoalQuery& query) {
        auto& [assocName, assocType, assocParams, associated, valueName, allowInferInputs, excludedImpl, coercions, operatorGoal, ambiguity] = query;
        (void)assocName;
        (void)assocType;
        (void)assocParams;
        (void)associated;
        (void)valueName;
        (void)allowInferInputs;
        (void)excludedImpl;
        (void)coercions;
        (void)operatorGoal;
        (void)ambiguity;
    }

    [[maybe_unused]] constexpr auto applySolverResponseApiGate = &Context::applySolverResponse;
}

STD_TEST_SUITE(HMTypeInferrenceSnapshot) {
    STD_TEST(testRollbackRestoresBinding) {
        auto pool = ObjPool::fromMemory();
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

        table.setIvarTo(a, types.primitive(HIRCoreType::U8));
        STD_INSIST(table.mutationGeneration != genInside);
        STD_INSIST(table.mutationGeneration != genBefore);
    }

    STD_TEST(testRollbackUndoesAliasAndTruncates) {
        auto pool = ObjPool::fromMemory();
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
        auto pool = ObjPool::fromMemory();
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
        auto pool = ObjPool::fromMemory();
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

    STD_TEST(testUnifyingValueWithItselfIsNoop) {
        auto pool = ObjPool::fromMemory();
        u32 id = 0;
        HIRTypeInterner types(*pool.mutPtr(), id);
        HMTypeInferrence table(types);

        const auto value = table.newIvarVal();
        const auto generation = table.mutationGeneration;
        table.ivarValUnify(value, value);

        STD_INSIST(!table.values.at(value).isAlias());
        STD_INSIST(table.getValue(value).as_Infer().index == value);
        STD_INSIST(table.mutationGeneration == generation);
    }

    STD_TEST(testCommitKeepsBindings) {
        auto pool = ObjPool::fromMemory();
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

        const auto b = table.newIvar();
        auto snap2 = table.snapshot();
        table.setIvarTo(b, types.primitive(HIRCoreType::U8));
        table.rollbackTo(snap2);
        STD_INSIST(table.getType(a)->is_Primitive());
        STD_INSIST(table.getType(b)->is_Infer());
    }

    STD_TEST(testNestedSnapshots) {
        auto pool = ObjPool::fromMemory();
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
        auto pool = ObjPool::fromMemory();
        u32 id = 0;
        HIRTypeInterner types(*pool.mutPtr(), id);
        HMTypeInferrence table(types);
        Span sp;

        const auto a = table.newIvar();
        const auto b = table.newIvar();
        Unifier unifier(sp, table);

        Vector<const HIRType*> pairTypes(2);
        pairTypes.pushBack(types.primitive(HIRCoreType::I32));
        pairTypes.pushBack(types.infer(b));
        const auto pairTy = types.tuple(std::move(pairTypes));
        STD_INSIST(unifier.unify(types.infer(a), pairTy) == Unifier::Outcome::Proven);
        STD_INSIST(unifier.unify(types.infer(b), types.primitive(HIRCoreType::U8)) == Unifier::Outcome::Proven);
        STD_INSIST(table.getType(b)->is_Primitive());
        STD_INSIST(unifier.pending().length() == 0);
    }

    STD_TEST(testUnifyMismatchRollsBack) {
        auto pool = ObjPool::fromMemory();
        u32 id = 0;
        HIRTypeInterner types(*pool.mutPtr(), id);
        HMTypeInferrence table(types);
        Span sp;

        const auto a = table.newIvar();
        Unifier unifier(sp, table);

        Vector<const HIRType*> leftTypes(2);
        leftTypes.pushBack(types.infer(a));
        leftTypes.pushBack(types.primitive(HIRCoreType::I32));
        const auto leftTy = types.tuple(std::move(leftTypes));
        Vector<const HIRType*> rightTypes(2);
        rightTypes.pushBack(types.primitive(HIRCoreType::U8));
        rightTypes.pushBack(types.primitive(HIRCoreType::U16));
        const auto rightTy = types.tuple(std::move(rightTypes));
        STD_INSIST(unifier.unify(leftTy, rightTy) == Unifier::Outcome::Mismatch);
        STD_INSIST(table.getType(a)->is_Infer());
    }

    STD_TEST(testUnifyOccursCheck) {
        auto pool = ObjPool::fromMemory();
        u32 id = 0;
        HIRTypeInterner types(*pool.mutPtr(), id);
        HMTypeInferrence table(types);
        Span sp;

        const auto a = table.newIvar();
        Unifier unifier(sp, table);

        Vector<const HIRType*> recursiveTypes(1);
        recursiveTypes.pushBack(types.infer(a));
        const auto recursive = types.tuple(std::move(recursiveTypes));
        STD_INSIST(unifier.unify(types.infer(a), recursive) == Unifier::Outcome::Mismatch);
        STD_INSIST(table.getType(a)->is_Infer());
    }

    STD_TEST(testUnifyLiteralClasses) {
        auto pool = ObjPool::fromMemory();
        u32 id = 0;
        HIRTypeInterner types(*pool.mutPtr(), id);
        HMTypeInferrence table(types);
        Span sp;

        const auto lit = table.newIvar(HIRInferClass::Integer);
        const auto fl = table.newIvar(HIRInferClass::Float);
        Unifier unifier(sp, table);

        STD_INSIST(unifier.unify(types.infer(lit), types.primitive(HIRCoreType::F64)) == Unifier::Outcome::Mismatch);
        STD_INSIST(unifier.unify(types.infer(lit), types.infer(fl)) == Unifier::Outcome::Mismatch);
        STD_INSIST(unifier.unify(types.infer(lit), types.primitive(HIRCoreType::U32)) == Unifier::Outcome::Proven);
        STD_INSIST(table.getType(lit)->is_Primitive());
    }

    STD_TEST(testUnifyDefersRigidUnknowns) {
        auto pool = ObjPool::fromMemory();
        u32 id = 0;
        HIRTypeInterner types(*pool.mutPtr(), id);
        HMTypeInferrence table(types);
        Span sp;

        Unifier unifier(sp, table);

        const auto placeholder = types.generic(RcString::newInterned("impl_?_test"), GENERICPlaceholder << 8);
        STD_INSIST(unifier.unify(placeholder, types.primitive(HIRCoreType::I32)) == Unifier::Outcome::Ambiguous);
        STD_INSIST(unifier.pending().length() == 1);
        STD_INSIST(unifier.pending()[0].right->is_Primitive() || unifier.pending()[0].left->is_Primitive());

        const auto canonical = types.infer(HIR_INFER_SOLVER_CANONICAL_MIN);
        STD_INSIST(unifier.unify(canonical, types.primitive(HIRCoreType::U8)) == Unifier::Outcome::Ambiguous);
        STD_INSIST(unifier.pending().length() == 2);

        const auto genericT = types.generic(RcString::newInterned("T"), 0);
        const auto genericU = types.generic(RcString::newInterned("U"), 1);
        STD_INSIST(unifier.unify(genericT, genericU) == Unifier::Outcome::Mismatch);
        STD_INSIST(unifier.pending().length() == 2);
    }

    STD_TEST(testUnifyBindsExistentialToCanonicalInput) {
        auto pool = ObjPool::fromMemory();
        u32 id = 0;
        HIRTypeInterner types(*pool.mutPtr(), id);
        HMTypeInferrence table(types);
        Span sp;

        const auto existential = table.newIvar();
        const auto canonical = types.infer(HIR_INFER_SOLVER_CANONICAL_MIN);
        Unifier unifier(sp, table);

        STD_INSIST(unifier.unify(types.infer(existential), canonical) == Unifier::Outcome::Proven);
        STD_INSIST(table.getType(types.infer(existential)) == canonical);
        STD_INSIST(unifier.pending().length() == 0);
    }

    STD_TEST(testSolverExistentialHasTypedBinderIdentity) {
        auto pool = ObjPool::fromMemory();
        u32 id = 0;
        HIRTypeInterner types(*pool.mutPtr(), id);

        const auto first = HIRGenericRef::newSolverExistential(41, 3);
        const auto same = HIRGenericRef::newSolverExistential(41, 3);
        const auto otherBinder = HIRGenericRef::newSolverExistential(42, 3);
        const auto otherIndex = HIRGenericRef::newSolverExistential(41, 4);
        const auto named = HIRGenericRef(RcString::newInterned("legacy"), GENERICPlaceholder, 3);

        STD_INSIST(first.isPlaceholder());
        STD_INSIST(first.isSolverExistential());
        STD_INSIST(first.group() == GENERICPlaceholder);
        STD_INSIST(first.idx() == 3);
        STD_INSIST(first == same);
        STD_INSIST(first != otherBinder);
        STD_INSIST(first != otherIndex);
        STD_INSIST(first != named);
        STD_INSIST(types.generic(first) == types.generic(same));
        STD_INSIST(types.generic(first) != types.generic(otherBinder));
    }

    STD_TEST(testCanonicalLiteralSlotRejectsStructuralType) {
        auto pool = ObjPool::fromMemory();
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
        STD_INSIST(unifier.unify(canonicalInteger, types.primitive(HIRCoreType::Usize)) == Unifier::Outcome::Ambiguous);
        STD_INSIST(unifier.pending().length() == 1);
    }

    STD_TEST(testLiteralSlotDefersProjectionBeforeClassCheck) {
        auto pool = ObjPool::fromMemory();
        u32 id = 0;
        HIRTypeInterner types(*pool.mutPtr(), id);
        HMTypeInferrence table(types);
        Span sp;

        const auto projection = types.path(HIRPath(types.primitive(HIRCoreType::U8), HIRGenericPath(), RcString::newInterned("Output")), HIRTypePathBinding::make_Opaque({}));

        const auto canonicalInteger = types.infer(HIR_INFER_SOLVER_CANONICAL_MIN, HIRInferClass::Integer);
        Unifier canonical(sp, table);
        STD_INSIST(canonical.unify(canonicalInteger, projection) == Unifier::Outcome::Ambiguous);
        STD_INSIST(canonical.pending().length() == 1);

        const auto liveInteger = table.newIvarTr(HIRInferClass::Integer);
        Unifier live(sp, table);
        STD_INSIST(live.unify(liveInteger, projection) == Unifier::Outcome::Ambiguous);
        STD_INSIST(live.pending().length() == 1);
        STD_INSIST(table.getType(liveInteger) == liveInteger);
    }

    STD_TEST(testUnifyArrayBindsConstLength) {
        auto pool = ObjPool::fromMemory();
        u32 id = 0;
        HIRTypeInterner types(*pool.mutPtr(), id);
        HMTypeInferrence table(types);
        Span sp;

        const auto length = table.newIvarVal();
        const auto element = types.primitive(HIRCoreType::U8);
        const auto genericArray = types.array(element, HIRConstGeneric::make_Infer({length}));
        const auto knownArray = types.array(element, 2);
        Unifier unifier(sp, table);

        STD_INSIST(unifier.unify(genericArray, knownArray) == Unifier::Outcome::Proven);
        const auto& resolved = table.getValue(HIRConstGeneric::make_Infer({length}));
        STD_INSIST(resolved.is_Evaluated());
        STD_INSIST(resolved.as_Evaluated()->readUsize(0) == 2);
        STD_INSIST(unifier.pendingValues().empty());
    }

    STD_TEST(testImplHeaderRelationMatchesProjectionInputs) {
        auto pool = ObjPool::fromMemory();
        u32 id = 0;
        HIRTypeInterner types(*pool.mutPtr(), id);
        HMTypeInferrence table(types);
        Span sp;

        const auto projection = [&](const HIRType* input) {
            HIRGenericPath trait;
            trait.params.types.push_back(input);
            return types.path(HIRPath(types.primitive(HIRCoreType::U8), ::std::move(trait), RcString::newInterned("Output")), HIRTypePathBinding::make_Opaque({}));
        };

        const auto rigidInput = types.generic(RcString::newInterned("T"), 0);
        const auto ordinarySlot = table.newIvarTr();
        Unifier ordinary(sp, table);
        STD_INSIST(ordinary.unify(projection(rigidInput), projection(ordinarySlot)) == Unifier::Outcome::Ambiguous);
        STD_INSIST(table.getType(ordinarySlot) == ordinarySlot);

        const auto candidateSlot = table.newIvarTr();
        Unifier candidate(sp, table, nullptr, {.relateProjectionInputs = true});
        STD_INSIST(candidate.unify(projection(rigidInput), projection(candidateSlot)) == Unifier::Outcome::Proven);
        STD_INSIST(table.getType(candidateSlot) == rigidInput);

        STD_INSIST(candidate.unify(projection(rigidInput), projection(types.primitive(HIRCoreType::U16))) == Unifier::Outcome::Mismatch);
        STD_INSIST(candidate.unify(rigidInput, projection(types.primitive(HIRCoreType::U16))) == Unifier::Outcome::Ambiguous);

        Unifier paramEnv(
            sp,
            table,
            nullptr,
            {
                .relateProjectionInputs = true,
                .rigidGenericsAreDistinct = true,
                .rigidProjectionsAreDistinct = true,
            }
        );
        STD_INSIST(paramEnv.unify(rigidInput, projection(types.primitive(HIRCoreType::U16))) == Unifier::Outcome::Mismatch);
        STD_INSIST(paramEnv.unify(projection(rigidInput), types.primitive(HIRCoreType::U16)) == Unifier::Outcome::Mismatch);
    }

    STD_TEST(testSolverProvenProjectionReplacesLiteralFallback) {
        auto pool = ObjPool::fromMemory();
        u32 id = 0;
        HIRTypeInterner types(*pool.mutPtr(), id);
        HMTypeInferrence table(types);

        const auto slot = table.newIvar(HIRInferClass::Integer);
        HIRGenericPath trait;
        const auto projection = types.path(HIRPath(types.primitive(HIRCoreType::U8), std::move(trait), RcString::newInterned("Output")), HIRTypePathBinding::make_Opaque({}));
        table.setIvarTo(slot, projection, true);
        STD_INSIST(table.getType(slot)->equalsIgnoringRegions(projection));
    }

    STD_TEST(testCandidateConstExistentialCapturesRigidPlaceholder) {
        auto pool = ObjPool::fromMemory();
        u32 id = 0;
        HIRTypeInterner types(*pool.mutPtr(), id);
        HMTypeInferrence table(types);
        Span sp;

        const auto ordinarySlot = table.newIvarVal();
        const auto candidateSlot = table.newIvarVal();
        const auto placeholder = HIRConstGeneric(HIRGenericRef(RcString::newInterned("const_?_test"), GENERICPlaceholder << 8));

        Unifier ordinary(sp, table);
        STD_INSIST(ordinary.unifyValues(placeholder, HIRConstGeneric::make_Infer({ordinarySlot})) == Unifier::Outcome::Ambiguous);
        STD_INSIST(ordinary.pendingValues().size() == 1);
        STD_INSIST(table.getValue(ordinarySlot).is_Infer());

        Unifier candidate(sp, table, nullptr, {.bindRigidValues = true});
        STD_INSIST(candidate.unifyValues(placeholder, HIRConstGeneric::make_Infer({candidateSlot})) == Unifier::Outcome::Proven);
        STD_INSIST(candidate.pendingValues().empty());
        STD_INSIST(table.getValue(candidateSlot) == placeholder);
    }

    STD_TEST(testRollbackRestoresChangedFlag) {
        auto pool = ObjPool::fromMemory();
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
