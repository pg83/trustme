#include "tagged_union_sample.h"

#include <std/tst/ut.h>

#include <cstring>
#include <utility>

using namespace stl;

STD_TEST_SUITE(TaggedUnionValue) {
    STD_TEST(defaultConstructsDefaultVariant) {
        SampleValue v;
        STD_INSIST(v.is_Empty());
        STD_INSIST(v.tag() == SampleValue::TAG_Empty);
        STD_INSIST(std::strcmp(v.tagStr(), "Empty") == 0);
        STD_INSIST(v.flags == 0);
        STD_INSIST(!v.is_Name());
        STD_INSIST(v.opt_Name() == nullptr);
    }

    STD_TEST(makeAndAccessors) {
        SampleValue v = SampleValue::make_Name("hello");
        STD_INSIST(v.is_Name());
        STD_INSIST(v.as_Name() == "hello");
        STD_INSIST(v.opt_Name() != nullptr && *v.opt_Name() == "hello");
        STD_INSIST(v.opt_Point() == nullptr);
        const SampleValue& cv = v;
        STD_INSIST(cv.as_Name() == "hello");
        STD_INSIST(cv.opt_Name() != nullptr);
    }

    STD_TEST(fieldPayloadAndInitializers) {
        SampleValue v = SampleValue::make_Point({.x = 3});
        STD_INSIST(v.is_Point());
        STD_INSIST(v.as_Point().x == 3);
        // The .tu spec gives y a default initializer.
        STD_INSIST(v.as_Point().y == 7);
    }

    STD_TEST(copyFromPayload) {
        SampleValue::Data_Point p;
        p.x = 1;
        p.y = 2;
        SampleValue a(p);
        SampleValue b = SampleValue::make_Point(p);
        STD_INSIST(a.as_Point().x == 1 && b.as_Point().y == 2);
    }

    STD_TEST(moveOnlyPayload) {
        SampleValue v = SampleValue::make_Owner(std::make_unique<int>(5));
        STD_INSIST(v.is_Owner());
        STD_INSIST(*v.as_Owner() == 5);
        int taken = *v.unwrap_Owner();
        STD_INSIST(taken == 5);
        // unwrap moves the payload's contents out; the variant stays active.
        STD_INSIST(v.is_Owner());
        STD_INSIST(v.as_Owner() == nullptr);
    }

    STD_TEST(moveTransfersPayloadAndExtraFields) {
        SampleValue v = SampleValue::make_Name("payload");
        v.flags = 9;
        SampleValue w = std::move(v);
        STD_INSIST(w.is_Name());
        STD_INSIST(w.as_Name() == "payload");
        STD_INSIST(w.flags == 9);
        STD_INSIST(v.isDead());
        STD_INSIST(std::strcmp(v.tagStr(), "ERR:DEAD") == 0);

        SampleValue u;
        u = std::move(w);
        STD_INSIST(u.is_Name() && u.as_Name() == "payload" && u.flags == 9);
        STD_INSIST(w.isDead());
    }

    STD_TEST(selfMoveAssignIsANoOp) {
        SampleValue v = SampleValue::make_Name("keep");
        SampleValue* alias = &v;
        v = std::move(*alias);
        STD_INSIST(v.is_Name());
        STD_INSIST(v.as_Name() == "keep");
    }

    STD_TEST(destructorRunsForLivePayload) {
        STD_INSIST(SampleCounted::liveCount == 0);
        {
            SampleValue v = SampleValue::make_Counted(SampleCounted(4));
            STD_INSIST(SampleCounted::liveCount == 1);
            STD_INSIST(v.as_Counted().value == 4);
        }
        STD_INSIST(SampleCounted::liveCount == 0);
    }

    STD_TEST(movedFromHuskIsNotDestructed) {
        // Historical TAGGED_UNION semantics, preserved deliberately: moving
        // marks the source dead without destructing the moved-from husk.
        // Resources travel to the destination, so nothing leaks, but the
        // husk's destructor never runs and the instance count shows it.
        STD_INSIST(SampleCounted::liveCount == 0);
        {
            SampleValue v = SampleValue::make_Counted(SampleCounted(4));
            STD_INSIST(SampleCounted::liveCount == 1);
            SampleValue w = std::move(v);
            STD_INSIST(SampleCounted::liveCount == 2);
        }
        STD_INSIST(SampleCounted::liveCount == 1);
        SampleCounted::liveCount = 0;
    }

    STD_TEST(extraDeclarationIsCallable) {
        SampleValue v = SampleValue::make_Name("x");
        v.flags = 3;
        STD_INSIST(v.extraProbe() == static_cast<int>(SampleValue::TAG_Name) * 100 + 3);
    }

    STD_TEST(assignReplacingPayloadDestructsOldOne) {
        STD_INSIST(SampleCounted::liveCount == 0);
        SampleValue v = SampleValue::make_Counted(SampleCounted(1));
        STD_INSIST(SampleCounted::liveCount == 1);
        v = SampleValue::make_Name("replacement");
        STD_INSIST(SampleCounted::liveCount == 0);
        STD_INSIST(v.is_Name());
    }
}

STD_TEST_SUITE(TaggedUnionIncomplete) {
    STD_TEST(emptyVariantsShareASingleton) {
        SampleTree a;
        SampleTree b;
        STD_INSIST(a.is_Nil() && b.is_Nil());
        // Empty variants are never allocated: both values expose the same
        // shared static instance.
        STD_INSIST(&a.as_Nil() == &b.as_Nil());
        STD_INSIST(a.opt_Nil() == &b.as_Nil());
    }

    STD_TEST(recursionThroughTheOwningStruct) {
        SampleTreeNode leaf;
        leaf.value = 1;
        SampleTreeNode root;
        root.value = 2;
        root.left = SampleTree::make_Node(std::move(leaf));
        STD_INSIST(root.left.is_Node());
        STD_INSIST(root.left.as_Node().value == 1);
        STD_INSIST(root.left.as_Node().left.is_Nil());
        STD_INSIST(root.right.is_Nil());

        SampleTree deep;
        for (int i = 0; i < 64; i++) {
            SampleTreeNode next;
            next.value = i;
            next.left = std::move(deep);
            deep = SampleTree::make_Node(std::move(next));
        }
        STD_INSIST(deep.is_Node());
        STD_INSIST(deep.as_Node().value == 63);
    }

    STD_TEST(moveIsAPointerSteal) {
        SampleTreeNode n;
        n.value = 5;
        SampleTree t = SampleTree::make_Node(std::move(n));
        const SampleTreeNode* payload = &t.as_Node();
        SampleTree u = std::move(t);
        // The payload does not move in memory: addresses taken before the
        // move stay valid, unlike the in-place storage.
        STD_INSIST(&u.as_Node() == payload);
        STD_INSIST(t.isDead());
        SampleTree w;
        w = std::move(u);
        STD_INSIST(&w.as_Node() == payload);
    }

    STD_TEST(generatedCloneIsDeep) {
        SampleTreeNode leaf;
        leaf.value = 7;
        SampleTreeNode root;
        root.value = 1;
        root.left = SampleTree::make_Node(std::move(leaf));
        SampleTree original = SampleTree::make_Node(std::move(root));

        SampleTree copy = original.clone();
        original.as_Node().value = 99;
        original.as_Node().left.as_Node().value = 98;
        STD_INSIST(copy.is_Node());
        STD_INSIST(copy.as_Node().value == 1);
        STD_INSIST(copy.as_Node().left.as_Node().value == 7);
        STD_INSIST(copy.as_Node().right.is_Nil());

        STD_INSIST(SampleTree().clone().is_Nil());

        int before = SampleCounted::liveCount;
        SampleTree marked = SampleTree::make_Mark(SampleCounted(3));
        SampleTree markedCopy = marked.clone();
        STD_INSIST(SampleCounted::liveCount == before + 2);
        STD_INSIST(markedCopy.as_Mark().value == 3);
    }

    STD_TEST(payloadIsDestructedWithTheUnion) {
        STD_INSIST(SampleCounted::liveCount == 0);
        {
            SampleTree t = SampleTree::make_Mark(SampleCounted(3));
            STD_INSIST(SampleCounted::liveCount == 1);
            STD_INSIST(t.as_Mark().value == 3);
            SampleTree u = std::move(t);
            // Pointer-steal move: no new payload instance, no husk.
            STD_INSIST(SampleCounted::liveCount == 1);
            STD_INSIST(u.as_Mark().value == 3);
        }
        STD_INSIST(SampleCounted::liveCount == 0);
    }
}
