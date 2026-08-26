#include "hir_typeck_helpers.h"

#include <std/mem/obj_pool.h>
#include <std/tst/ut.h>

using namespace stl;

STD_TEST_SUITE(HMTypeInferrenceSnapshot) {
    STD_TEST(testRollbackRestoresBinding) {
        auto pool = stl::ObjPool::fromMemory();
        HIRTypeInterner types(*pool.mutPtr());
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
        HIRTypeInterner types(*pool.mutPtr());
        HMTypeInferrence table(types);

        const auto a = table.newIvar();
        const auto b = table.newIvar();
        const auto sizeBefore = table.ivars.size();

        auto snap = table.snapshot();
        const auto c = table.newIvar();
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
    }

    STD_TEST(testRollbackRestoresLiteralClassUpgrade) {
        auto pool = stl::ObjPool::fromMemory();
        HIRTypeInterner types(*pool.mutPtr());
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
        HIRTypeInterner types(*pool.mutPtr());
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
        HIRTypeInterner types(*pool.mutPtr());
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
        HIRTypeInterner types(*pool.mutPtr());
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

    STD_TEST(testRollbackRestoresChangedFlag) {
        auto pool = stl::ObjPool::fromMemory();
        HIRTypeInterner types(*pool.mutPtr());
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
