#pragma once

//#include "cpp_unpack.h"
#include <cassert>
#include <string>
#include <stdexcept>

#define TU_FIRST(a, ...) a
#define TU_EXP1(x) x
#define TU_EXP(...) __VA_ARGS__

#define TU_CASE_ITEM(src, mod, var, name) \
    mod auto& name = src.as_##var();      \
    (void)&name;
#define TU_CASE_BODY(class, var, ...) \
    case class ::var: {               \
        __VA_ARGS__                   \
    } break;
#define TU_CASE(mod, class, var, name, src, ...) TU_CASE_BODY(mod, class, var, TU_CASE_ITEM(src, mod, var, name) __VA_ARGS__)
#define TU_CASE2(mod, class, var, n1, s1, n2, s2, ...) TU_CASE_BODY(mod, class, var, TU_CASE_ITEM(s1, mod, var, n1) TU_CASE_ITEM(s2, mod, var, n2) __VA_ARGS__)

// Argument iteration
#define TU_DISP0(n)
#define TU_DISP1(n, _1) n _1
#define TU_DISP2(n, _1, _2) n _1 n _2
#define TU_DISP3(n, v, v2, v3) n v n v2 n v3
#define TU_DISP4(n, v, v2, v3, v4) n v n v2 n v3 n v4
#define TU_DISP5(n, a1, a2, a3, b1, b2) TU_DISP3(n, a1, a2, a3) TU_DISP2(n, b1, b2)
#define TU_DISP6(n, a1, a2, a3, b1, b2, b3) TU_DISP3(n, a1, a2, a3) TU_DISP3(n, b1, b2, b3)
#define TU_DISP7(n, a1, a2, a3, a4, b1, b2, b3) TU_DISP4(n, a1, a2, a3, a4) TU_DISP3(n, b1, b2, b3)
#define TU_DISP8(n, a1, a2, a3, a4, b1, b2, b3, b4) TU_DISP4(n, a1, a2, a3, a4) TU_DISP4(n, b1, b2, b3, b4)
#define TU_DISP9(n, a1, a2, a3, a4, b1, b2, b3, b4, c1) TU_DISP4(n, a1, a2, a3, a4) TU_DISP3(n, b1, b2, b3) TU_DISP2(n, b4, c1)
#define TU_DISP10(n, a1, a2, a3, a4, b1, b2, b3, b4, c1, c2) TU_DISP4(n, a1, a2, a3, a4) TU_DISP4(n, b1, b2, b3, b4) TU_DISP2(n, c1, c2)
#define TU_DISP11(n, a1, a2, a3, a4, b1, b2, b3, b4, c1, c2, c3) TU_DISP4(n, a1, a2, a3, a4) TU_DISP4(n, b1, b2, b3, b4) TU_DISP3(n, c1, c2, c3)
#define TU_DISP12(n, a1, a2, a3, a4, b1, b2, b3, b4, c1, c2, c3, c4) TU_DISP4(n, a1, a2, a3, a4) TU_DISP4(n, b1, b2, b3, b4) TU_DISP4(n, c1, c2, c3, c4)
#define TU_DISP13(n, a1, a2, a3, a4, a5, b1, b2, b3, b4, c1, c2, c3, c4) TU_DISP5(n, a1, a2, a3, a4, a5) TU_DISP4(n, b1, b2, b3, b4) TU_DISP4(n, c1, c2, c3, c4)
#define TU_DISP14(n, a1, a2, a3, a4, a5, b1, b2, b3, b4, b5, c1, c2, c3, c4) TU_DISP5(n, a1, a2, a3, a4, a5) TU_DISP5(n, b1, b2, b3, b4, b5) TU_DISP4(n, c1, c2, c3, c4)
#define TU_DISP15(n, a1, a2, a3, a4, a5, b1, b2, b3, b4, b5, c1, c2, c3, c4, c5) TU_DISP5(n, a1, a2, a3, a4, a5) TU_DISP5(n, b1, b2, b3, b4, b5) TU_DISP5(n, c1, c2, c3, c4, c5)
#define TU_DISP16(n, a1, a2, a3, a4, a5, b1, b2, b3, b4, b5, c1, c2, c3, c4, c5, d1) TU_DISP5(n, a1, a2, a3, a4, a5) TU_DISP5(n, b1, b2, b3, b4, b5) TU_DISP5(n, c1, c2, c3, c4, c5) TU_DISP1(n, d1)
#define TU_DISP17(n, a1, a2, a3, a4, a5, b1, b2, b3, b4, b5, c1, c2, c3, c4, c5, d1, d2) TU_DISP5(n, a1, a2, a3, a4, a5) TU_DISP5(n, b1, b2, b3, b4, b5) TU_DISP5(n, c1, c2, c3, c4, c5) TU_DISP2(n, d1, d2)
#define TU_DISP18(n, a1, a2, a3, a4, a5, b1, b2, b3, b4, b5, c1, c2, c3, c4, c5, d1, d2, d3) TU_DISP5(n, a1, a2, a3, a4, a5) TU_DISP5(n, b1, b2, b3, b4, b5) TU_DISP5(n, c1, c2, c3, c4, c5) TU_DISP3(n, d1, d2, d3)

#define TU_DISPO0(n)
#define TU_DISPO1(n, _1) n(_1)
#define TU_DISPO2(n, _1, _2) n(_1) n(_2)
#define TU_DISPO3(n, v, v2, v3) n(v) n(v2) n(v3)
#define TU_DISPO4(n, v, v2, v3, v4) n(v) n(v2) n(v3) n(v4)
#define TU_DISPO5(n, a1, a2, a3, b1, b2) TU_DISPO3(n, a1, a2, a3) TU_DISPO2(n, b1, b2)
#define TU_DISPO6(n, a1, a2, a3, b1, b2, b3) TU_DISPO3(n, a1, a2, a3) TU_DISPO3(n, b1, b2, b3)
#define TU_DISPO7(n, a1, a2, a3, a4, b1, b2, b3) TU_DISPO4(n, a1, a2, a3, a4) TU_DISPO3(n, b1, b2, b3)
#define TU_DISPO8(n, a1, a2, a3, a4, b1, b2, b3, b4) TU_DISPO4(n, a1, a2, a3, a4) TU_DISPO4(n, b1, b2, b3, b4)
#define TU_DISPO9(n, a1, a2, a3, a4, b1, b2, b3, b4, c1) TU_DISPO4(n, a1, a2, a3, a4) TU_DISPO3(n, b1, b2, b3) TU_DISPO2(n, b4, c1)
#define TU_DISPO10(n, a1, a2, a3, a4, b1, b2, b3, b4, c1, c2) TU_DISPO4(n, a1, a2, a3, a4) TU_DISPO4(n, b1, b2, b3, b4) TU_DISPO2(n, c1, c2)
#define TU_DISPO11(n, a1, a2, a3, a4, b1, b2, b3, b4, c1, c2, c3) TU_DISPO4(n, a1, a2, a3, a4) TU_DISPO4(n, b1, b2, b3, b4) TU_DISPO3(n, c1, c2, c3)
#define TU_DISPO12(n, a1, a2, a3, a4, b1, b2, b3, b4, c1, c2, c3, c4) TU_DISPO4(n, a1, a2, a3, a4) TU_DISPO4(n, b1, b2, b3, b4) TU_DISPO4(n, c1, c2, c3, c4)
#define TU_DISPO13(n, a1, a2, a3, a4, a5, b1, b2, b3, b4, c1, c2, c3, c4) TU_DISPO5(n, a1, a2, a3, a4, a5) TU_DISPO4(n, b1, b2, b3, b4) TU_DISPO4(n, c1, c2, c3, c4)
#define TU_DISPO14(n, a1, a2, a3, a4, a5, b1, b2, b3, b4, b5, c1, c2, c3, c4) TU_DISPO5(n, a1, a2, a3, a4, a5) TU_DISPO5(n, b1, b2, b3, b4, b5) TU_DISPO4(n, c1, c2, c3, c4)
#define TU_DISPO15(n, a1, a2, a3, a4, a5, b1, b2, b3, b4, b5, c1, c2, c3, c4, c5) TU_DISPO5(n, a1, a2, a3, a4, a5) TU_DISPO5(n, b1, b2, b3, b4, b5) TU_DISPO5(n, c1, c2, c3, c4, c5)
#define TU_DISPO16(n, a1, a2, a3, a4, a5, b1, b2, b3, b4, b5, c1, c2, c3, c4, c5, d1) TU_DISPO5(n, a1, a2, a3, a4, a5) TU_DISPO5(n, b1, b2, b3, b4, b5) TU_DISPO5(n, c1, c2, c3, c4, c5) TU_DISPO1(n, d1)
#define TU_DISPO17(n, a1, a2, a3, a4, a5, b1, b2, b3, b4, b5, c1, c2, c3, c4, c5, d1, d2) TU_DISPO5(n, a1, a2, a3, a4, a5) TU_DISPO5(n, b1, b2, b3, b4, b5) TU_DISPO5(n, c1, c2, c3, c4, c5) TU_DISPO2(n, d1, d2)
#define TU_DISPO18(n, a1, a2, a3, a4, a5, b1, b2, b3, b4, b5, c1, c2, c3, c4, c5, d1, d2, d3) TU_DISPO5(n, a1, a2, a3, a4, a5) TU_DISPO5(n, b1, b2, b3, b4, b5) TU_DISPO5(n, c1, c2, c3, c4, c5) TU_DISPO4(n, d1, d2, d3)

#define TU_DISPA(n, a) n a
#define TU_DISPA1(n, a, _1) TU_DISPA(n, (TU_EXP a, TU_EXP _1))
#define TU_DISPA2(n, a, _1, _2) TU_DISPA(n, (TU_EXP a, TU_EXP _1)) TU_DISPA(n, (TU_EXP a, TU_EXP _2))
#define TU_DISPA3(n, a, _1, _2, _3) TU_DISPA(n, (TU_EXP a, TU_EXP _1)) TU_DISPA(n, (TU_EXP a, TU_EXP _2)) TU_DISPA(n, (TU_EXP a, TU_EXP _3))
#define TU_DISPA4(n, a, a1, a2, b1, b2) TU_DISPA2(n, a, a1, a2) TU_DISPA2(n, a, b1, b2)
#define TU_DISPA5(n, a, a1, a2, a3, b1, b2) TU_DISPA3(n, a, a1, a2, a3) TU_DISPA2(n, a, b1, b2)
#define TU_DISPA6(n, a, a1, a2, a3, b1, b2, b3) TU_DISPA3(n, a, a1, a2, a3) TU_DISPA3(n, a, b1, b2, b3)
#define TU_DISPA7(n, a, a1, a2, a3, b1, b2, c1, c2) TU_DISPA3(n, a, a1, a2, a3) TU_DISPA2(n, a, b1, b2) TU_DISPA2(n, a, c1, c2)
#define TU_DISPA8(n, a, a1, a2, a3, b1, b2, b3, c1, c2) TU_DISPA3(n, a, a1, a2, a3) TU_DISPA3(n, a, b1, b2, b3) TU_DISPA2(n, a, c1, c2)
#define TU_DISPA9(n, a, a1, a2, a3, b1, b2, b3, c1, c2, c3) TU_DISPA3(n, a, a1, a2, a3) TU_DISPA3(n, a, b1, b2, b3) TU_DISPA3(n, a, c1, c2, c3)
#define TU_DISPA10(n, a, a1, a2, a3, b1, b2, b3, c1, c2, c3, d1) TU_DISPA3(n, a, a1, a2, a3) TU_DISPA3(n, a, b1, b2, b3) TU_DISPA3(n, a, c1, c2, c3) TU_DISPA(n, (TU_EXP a, TU_EXP d1))
#define TU_DISPA11(n, a, a1, a2, a3, b1, b2, b3, c1, c2, c3, d1, d2) TU_DISPA3(n, a, a1, a2, a3) TU_DISPA3(n, a, b1, b2, b3) TU_DISPA3(n, a, c1, c2, c3) TU_DISPA2(n, a, d1, d2)
#define TU_DISPA12(n, a, a1, a2, a3, b1, b2, b3, c1, c2, c3, d1, d2, d3) TU_DISPA3(n, a, a1, a2, a3) TU_DISPA3(n, a, b1, b2, b3) TU_DISPA3(n, a, c1, c2, c3) TU_DISPA3(n, a, d1, d2, d3)
#define TU_DISPA13(n, a, a1, a2, a3, a4, b1, b2, b3, c1, c2, c3, d1, d2, d3) TU_DISPA4(n, a, a1, a2, a3, a4) TU_DISPA3(n, a, b1, b2, b3) TU_DISPA3(n, a, c1, c2, c3) TU_DISPA3(n, a, d1, d2, d3)
#define TU_DISPA14(n, a, a1, a2, a3, a4, b1, b2, b3, b4, c1, c2, c3, d1, d2, d3) TU_DISPA4(n, a, a1, a2, a3, a4) TU_DISPA4(n, a, b1, b2, b3, b4) TU_DISPA3(n, a, c1, c2, c3) TU_DISPA3(n, a, d1, d2, d3)
#define TU_DISPA15(n, a, a1, a2, a3, a4, b1, b2, b3, b4, c1, c2, c3, c4, d1, d2, d3) TU_DISPA4(n, a, a1, a2, a3, a4) TU_DISPA4(n, a, b1, b2, b3, b4) TU_DISPA4(n, a, c1, c2, c3, c4) TU_DISPA3(n, a, d1, d2, d3)
#define TU_DISPA16(n, a, a1, a2, a3, a4, b1, b2, b3, b4, c1, c2, c3, c4, d1, d2, d3, d4) TU_DISPA4(n, a, a1, a2, a3, a4) TU_DISPA4(n, a, b1, b2, b3, b4) TU_DISPA4(n, a, c1, c2, c3, c4) TU_DISPA4(n, a, d1, d2, d3, d4)
#define TU_DISPA17(n, a, a1, a2, a3, a4, b1, b2, b3, b4, c1, c2, c3, c4, d1, d2, d3, d4, e1) TU_DISPA4(n, a, a1, a2, a3, a4) TU_DISPA4(n, a, b1, b2, b3, b4) TU_DISPA4(n, a, c1, c2, c3, c4) TU_DISPA4(n, a, d1, d2, d3, d4) TU_DISPA1(n, a, e1)
#define TU_DISPA18(n, a, a1, a2, a3, a4, b1, b2, b3, b4, c1, c2, c3, c4, d1, d2, d3, d4, e1, e2) TU_DISPA4(n, a, a1, a2, a3, a4) TU_DISPA4(n, a, b1, b2, b3, b4) TU_DISPA4(n, a, c1, c2, c3, c4) TU_DISPA4(n, a, d1, d2, d3, d4) TU_DISPA2(n, a, e1, e2)

// Macro to obtain a numbered macro for argument counts
// - Raw variant
#define TU_GM_I(SUF, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, COUNT, ...) SUF##COUNT
#define TU_GM(SUF, ...) TU_EXP1(TU_GM_I(SUF, __VA_ARGS__, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0))
// - _DISP based variant (for iteration)
#define TU_GMX(...) TU_EXP1(TU_GM(TU_DISP, __VA_ARGS__))
#define TU_GMO(...) TU_EXP1(TU_GM(TU_DISPO, __VA_ARGS__))
#define TU_GMA(...) TU_EXP1(TU_GM(TU_DISPA, __VA_ARGS__))

// TODO: use `decltype` in place of the `class` argument to TU_MATCH/TU_IFLET
// "match"-like statement
// TU_MATCH(Class, m_data, ent, (Variant, CODE), (Variant2, CODE))
#define TU_MATCHA(VARS, NAMES, ...) TU_MATCH(::std::remove_reference<decltype(TU_FIRST VARS)>::type, VARS, NAMES, __VA_ARGS__)
#define TU_MATCH(CLASS, VAR, NAME, ...)                        \
    switch ((TU_FIRST VAR).tag()) { /*
*/                         \
        case CLASS::TAGDEAD:                                   \
            assert(!"ERROR: destructed tagged union used"); /*
*/ \
            TU_MATCH_ARMS(CLASS, VAR, NAME, __VA_ARGS__)    /*
*/ \
    }
#define TU_MATCH_DEF(CLASS, VAR, NAME, DEF, ...)               \
    switch ((TU_FIRST VAR).tag()) { /*
*/                         \
        case CLASS::TAGDEAD:                                   \
            assert(!"ERROR: destructed tagged union used"); /*
*/ \
            TU_MATCH_ARMS(CLASS, VAR, NAME, __VA_ARGS__)       \
        /*
*/                                                     \
        default: {                                             \
            TU_EXP DEF;                                        \
        } break; /*
*/                                            \
    }
#define TU_MATCH_BIND1(TAG, VAR, NAME) /*MATCH_BIND*/   \
    decltype((VAR).as_##TAG()) NAME = (VAR).as_##TAG(); \
    (void)&NAME;
#define TU_MATCH_BIND2_(TAG, v1, v2, n1, n2) TU_MATCH_BIND1(TAG, v1, n1) TU_MATCH_BIND1(TAG, v2, n2)
#define TU_MATCH_BIND2(...) TU_EXP1(TU_MATCH_BIND2_(__VA_ARGS__)) // << Exists to cause expansion of the vars
#define TU_MATCH_ARM(CLASS, VAR, NAME, TAG, ...) \
    case CLASS::TAG_##TAG: { /*
*/                  \
        TU_GM(TU_MATCH_BIND, TU_EXP VAR)(        \
            TAG,                                 \
            TU_EXP VAR,                          \
            TU_EXP NAME                          \
        ) /*
*/ __VA_ARGS__ /*
*/                      \
    } break;
#define TU_MATCH_ARMS(CLASS, VAR, NAME, ...) TU_EXP1(TU_GMA(__VA_ARGS__)(TU_MATCH_ARM, (CLASS, VAR, NAME), __VA_ARGS__))

#define TU_IFLET(CLASS, VAR, TAG, NAME, ...) \
    if ((VAR).tag() == CLASS::TAG_##TAG) {   \
        auto& NAME = (VAR).as_##TAG();       \
        (void)&NAME;                         \
        __VA_ARGS__                          \
    }

#define TU_MATCH_HDR(VARS, brace) TU_MATCH_HDR_(::std::remove_reference<decltype(TU_FIRST VARS)>::type, VARS, brace)
#define TU_MATCH_HDR_(CLASS, VARS, brace) \
    switch ((TU_FIRST VARS).tag())        \
    brace case CLASS::TAGDEAD:            \
        assert(!"ERROR: destructed tagged union used");
// Nested single-iteration loops provide a declaration scope for the arm binding.
#define TU_ARM(VAR, TAG, NAME)                                    \
    break;                                                        \
    case ::std::remove_reference<decltype(VAR)>::type::TAG_##TAG: \
        for (bool tuLc = true; tuLc; tuLc = false)             \
            for (decltype((VAR).as_##TAG()) NAME = (VAR).as_##TAG(); (void)NAME, tuLc; tuLc = false)

#define TU_MATCH_HDRA(VARS, brace) TU_MATCH_HDRA_(::std::remove_reference<decltype(TU_FIRST VARS)>::type, VARS, brace)
#define TU_MATCH_HDRA_(CLASS, VARS, brace) /*
    */                           \
    for (bool tuLc = true; tuLc; tuLc = false)                       \
        for (TU_EXP1(TUMATCHHDRADecl VARS); tuLc; tuLc = false) /*
        */ \
            switch (tuMatchHdr2V.tag())                              \
            brace /*
        */                                                    \
                case CLASS::TAGDEAD:                                    \
                assert(!"ERROR: destructed tagged union used");
#define TUMATCHHDRADeclRest1(v1) &tuMatchHdr2V = v1
#define TUMATCHHDRADeclRest2(v1, v2) TUMATCHHDRADeclRest1(v1), &tuMatchHdr2V2 = v2
#define TUMATCHHDRADeclRest3(v1, v2, v3) TUMATCHHDRADeclRest2(_, v2), &tuMatchHdr2V3 = v3
#define TUMATCHHDRADecl(...) auto TU_EXP1(TU_GM(TUMATCHHDRADeclRest, __VA_ARGS__)(__VA_ARGS__))
#define TUARMADeclInner1(TAG, v1) v1 = tuMatchHdr2V.as_##TAG()
#define TUARMADeclInner2(TAG, v1, v2) TUARMADeclInner1(TAG, v1), v2 = tuMatchHdr2V2.as_##TAG()
#define TUARMADeclInner3(TAG, v1, v2, v3) TUARMADeclInner1(TAG, v1, v2), v3 = tuMatchHdr2V3.as_##TAG()
#define TUARMADecl(TAG, ...) decltype(tuMatchHdr2V.as_##TAG()) TU_EXP1(TU_GM(TUARMADeclInner, __VA_ARGS__)(TAG, __VA_ARGS__))
#define TUARMAIgnVal(v) (void)v,
// Nested single-iteration loops provide a declaration scope for the arm bindings.
#define TU_ARMA(TAG, ...)                                                        \
    break;                                                                       \
    case ::std::remove_reference<decltype(tuMatchHdr2V)>::type::TAG_##TAG: /*
    */ \
        for (bool tuLc = true; tuLc; tuLc = false)                            \
            for (TUARMADecl(TAG, __VA_ARGS__); TU_EXP1(TU_GMO(__VA_ARGS__)(TUARMAIgnVal, __VA_ARGS__)) tuLc; tuLc = false)

//#define TU_TEST(VAL, ...)    (VAL.is_##TAG() && VAL.as_##TAG() TEST)
#define TU_TEST1(VAL, TAG1, TEST) ((VAL).is_##TAG1() && ((VAL).as_##TAG1() TEST))
#define TU_TEST2(VAL, TAG1, FLD1, TAG2, TEST) ((VAL).is_##TAG1() && (VAL).as_##TAG1() FLD1.is_##TAG2() && (VAL).as_##TAG1() FLD1.as_##TAG2() TEST)
#define TU_OPT1(VAL, TAG1, GET) ((VAL).is_##TAG1() ? ((VAL).as_##TAG1() GET) : nullptr)

#define TU_DATANAME(name) Data_##name
// Internals of TU_CONS
#define TU_CONS_I(__name, __tag, __type)               \
    __name(__type&& v)                                 \
        : tag_(TAG_##__tag) {                         \
        TUMoveInplace(mData.__tag, ::std::move(v)); \
    }                                                  \
    template <typename _TU_Dummy = void>               \
    __name(const __type& v)                            \
        : tag_(TAG_##__tag) {                         \
        TUCopyInplace(mData.__tag, v);              \
    }                                                  \
    static selfT make_##__tag(__type&& v) {           \
        return __name(::std::move(v));                 \
    }                                                  \
    template <typename _TU_Dummy = void>               \
    static selfT make_##__tag(const __type& v) {      \
        return __name(v);                              \
    }                                                  \
    bool is_##__tag() const {                          \
        return tag_ == TAG_##__tag;                   \
    }                                                  \
    const __type* opt_##__tag() const {                \
        if (tag_ == TAG_##__tag)                      \
            return &mData.__tag;                      \
        return nullptr;                                \
    }                                                  \
    __type* opt_##__tag() {                            \
        if (tag_ == TAG_##__tag)                      \
            return &mData.__tag;                      \
        return nullptr;                                \
    }                                                  \
    const __type& as_##__tag() const {                 \
        assert(tag_ == TAG_##__tag);                  \
        return mData.__tag;                           \
    }                                                  \
    __type& as_##__tag() {                             \
        assert(tag_ == TAG_##__tag);                  \
        return mData.__tag;                           \
    }                                                  \
    template <typename _TU_Type = __type>              \
    _TU_Type unwrap_##__tag() {                        \
        return ::std::move(this->as_##__tag());        \
    }                                                  \
// Define a tagged union constructor
#define TU_CONS(__name, name, ...) TU_CONS_I(__name, name, TU_DATANAME(name))

// Declarations used when a tagged union owns recursive types that are not
// complete until later in the translation unit.
#define TU_CONS_I_DECL(__name, __tag, __type) \
    __name(__type v);                         \
    static selfT make_##__tag(__type v);     \
    bool is_##__tag() const {                 \
        return tag_ == TAG_##__tag;          \
    }                                         \
    const __type* opt_##__tag() const {       \
        if (tag_ == TAG_##__tag)             \
            return &mData.__tag;             \
        return nullptr;                       \
    }                                         \
    __type* opt_##__tag() {                   \
        if (tag_ == TAG_##__tag)             \
            return &mData.__tag;             \
        return nullptr;                       \
    }                                         \
    const __type& as_##__tag() const {        \
        assert(tag_ == TAG_##__tag);         \
        return mData.__tag;                  \
    }                                         \
    __type& as_##__tag() {                    \
        assert(tag_ == TAG_##__tag);         \
        return mData.__tag;                  \
    }                                         \
    __type unwrap_##__tag();

#define TU_CONS_DECL(__name, name, ...) TU_CONS_I_DECL(__name, name, TU_DATANAME(name))

#define TU_CONS_IMPL(__name, name, ...)                               \
    __name::__name(__name::TU_DATANAME(name) v)                       \
        : tag_(TAG_##name) {                                         \
        new (&mData.name) __name::TU_DATANAME(name)(::std::move(v)); \
    }                                                                 \
    __name __name::make_##name(__name::TU_DATANAME(name) v) {         \
        return __name(::std::move(v));                                \
    }                                                                 \
    __name::TU_DATANAME(name) __name::unwrap_##name() {               \
        return ::std::move(this->as_##name());                        \
    }

// Type definitions_
#define TU_TYPEDEF(name, ...) \
    typedef __VA_ARGS__ TU_DATANAME(name); /*
*/

#define TU_TAG(name, ...) TAG_##name,

// Destructor internals
#define TU_DEST_CASE(tag, ...)           \
    case TAG_##tag:                      \
        TUDestructInplace(mData.tag); \
        break; /*
*/

// move constructor internals
#define TU_MOVE_CASE(tag, ...)                                  \
    case TAG_##tag:                                             \
        TUMoveInplace(mData.tag, ::std::move(x.mData.tag)); \
        break; /*
*/

// "tag_to_str" internals
#define TU_TOSTR_CASE(tag, ...) \
    case TAG_##tag:             \
        return #tag; /*
*/
// "tag_from_str" internals
#define TU_FROMSTR_CASE(tag, ...) \
    else if (str == #tag) return TAG_##tag; /*
*/
#define TU_FROMSTR_CASES(...) TU_EXP1(TU_GMX(__VA_ARGS__)(TU_FROMSTR_CASE, __VA_ARGS__))

#define TU_UNION_FIELD(tag, ...) \
    TU_DATANAME(tag)             \
    tag; /*
*/
#define TU_UNION_FIELDS(...) TU_EXP1(TU_GMX(__VA_ARGS__)(TU_UNION_FIELD, __VA_ARGS__))

#define TU_CONSS(_name, ...) TU_EXP1(TU_GMA(__VA_ARGS__)(TU_CONS, (_name), __VA_ARGS__))
#define TU_CONSS_DECL(_name, ...) TU_EXP1(TU_GMA(__VA_ARGS__)(TU_CONS_DECL, (_name), __VA_ARGS__))
#define TU_CONSS_IMPL(_name, ...) TU_EXP1(TU_GMA(__VA_ARGS__)(TU_CONS_IMPL, (_name), __VA_ARGS__))
#define TU_TYPEDEFS(...) TU_EXP1(TU_GMX(__VA_ARGS__)(TU_TYPEDEF, __VA_ARGS__))
#define TU_TAGS(...) TU_EXP1(TU_GMX(__VA_ARGS__)(TU_TAG, __VA_ARGS__))
#define TU_DEST_CASES(...) TU_EXP1(TU_GMX(__VA_ARGS__)(TU_DEST_CASE, __VA_ARGS__))
#define TU_MOVE_CASES(...) TU_EXP1(TU_GMX(__VA_ARGS__)(TU_MOVE_CASE, __VA_ARGS__))
#define TU_TOSTR_CASES(...) TU_EXP1(TU_GMX(__VA_ARGS__)(TU_TOSTR_CASE, __VA_ARGS__))

/**
 * Define a new tagged union
 *
 * ```
 * TAGGED_UNION(Inner, Any,
 *     (Any, (struct { bool match_multiple; })),
 *     (Tuple, (::std::vector<Pattern> )),
 *     (TupleStruct, (struct { Path path; ::std::vector<Pattern> sub_patterns; })),
 *     (Value, (::std::unique_ptr<ExprNode> )),
 *     (Range, (struct { ::std::unique_ptr<ExprNode> left; ::std::unique_ptr<ExprNode> right; }))
 *     );
 * ```
 */
#define TAGGED_UNION(_name, _def, ...) TU_EXP1(TAGGED_UNION_EX(_name, (), _def, (TU_EXP(__VA_ARGS__)), (), (), ()))
#if defined(__clang__)
    #define TAGGED_UNION_EX(_name, _inherit, _def, _variants, _extra_move, _extra_assign, _extra) \
        _Pragma("clang diagnostic push");                                                         \
        _Pragma("clang diagnostic ignored \"-Wnon-c-typedef-for-linkage\"");                      \
        _TAGGED_UNION_EX(_name, _inherit, _def, _variants, _extra_move, _extra_assign, _extra)    \
        _Pragma("clang diagnostic pop");
#else
    #define TAGGED_UNION_EX(_name, _inherit, _def, _variants, _extra_move, _extra_assign, _extra) _TAGGED_UNION_EX(_name, _inherit, _def, _variants, _extra_move, _extra_assign, _extra)
#endif

#define _TAGGED_UNION_EX(_name, _inherit, _def, _variants, _extra_move, _extra_assign, _extra) \
    class _name TU_EXP _inherit {                                                              \
        typedef _name selfT; /*
*/                                                               \
    public:                                                                                    \
        TU_TYPEDEFS _variants /*
*/                                                               \
            enum Tag {                                                                         \
            TAGDEAD,                                                                           \
            TU_TAGS _variants                                                                  \
        }; /*
*/                                                                                  \
    private:                                                                                   \
        Tag tag_;                                                                             \
        union DataUnion {                                                                      \
            TU_UNION_FIELDS _variants DataUnion() {                                            \
            }                                                                                  \
            ~DataUnion() {                                                                     \
            }                                                                                  \
        } mData; /*
*/                                                                           \
    public:                                                                                    \
        _name()                                                                                \
            : tag_(TAG_##_def) {                                                              \
            new (&mData._def) TU_DATANAME(_def)();                                            \
        } /*
*/                                                                                   \
        _name(const _name&) = delete; /*
*/                                                       \
        _name(_name&& x) noexcept                                                              \
            : tag_(x.tag_) TU_EXP _extra_move {                                              \
            switch (tag_) {                                                                   \
                case TAGDEAD:                                                                  \
                    break;                                                                     \
                    TU_MOVE_CASES _variants                                                    \
            }                                                                                  \
            x.tag_ = TAGDEAD;                                                                 \
        } /*
*/                                                                                   \
        _name& operator=(_name&& x) {                                                          \
            switch (tag_) {                                                                   \
                case TAGDEAD:                                                                  \
                    break;                                                                     \
                    TU_DEST_CASES _variants                                                    \
            }                                                                                  \
            tag_ = x.tag_;                                                                   \
            TU_EXP _extra_assign switch (tag_) {                                              \
                case TAGDEAD:                                                                  \
                    break;                                                                     \
                    TU_MOVE_CASES _variants                                                    \
            };                                                                                 \
            return *this;                                                                      \
        } /*
*/                                                                                   \
        ~_name() {                                                                             \
            switch (tag_) {                                                                   \
                case TAGDEAD:                                                                  \
                    break;                                                                     \
                    TU_DEST_CASES _variants                                                    \
            }                                                                                  \
            tag_ = TAGDEAD;                                                                   \
        }                                                                                      \
                                                                                               \
        Tag tag() const {                                                                      \
            return tag_;                                                                      \
        }                                                                                      \
        const char* tagStr() const {                                                          \
            return tagToStr(tag_);                                                          \
        }                                                                                      \
        TU_CONSS(_name, TU_EXP _variants)                                                      \
        /*
*/                                                                                     \
        static const char* tagToStr(Tag tag) {                                               \
            switch (tag) { /*
*/                                                                  \
                case TAGDEAD:                                                                  \
                    return "ERR:DEAD";       /*
*/                                                \
                    TU_TOSTR_CASES _variants /*
*/                                                \
            }                                                                                  \
            return "";                                                                         \
        } /*
*/                                                                                   \
        static Tag tagFromStr(const ::std::string& str) {                                    \
            if (0)                                                                             \
                ;                      /*
*/                                                      \
            TU_FROMSTR_CASES _variants /*
*/                                                      \
                else throw ::std::runtime_error("enum " #_name " No conversion");              \
        }                                                                                      \
        TU_EXP _extra                                                                          \
    }

/**
 * Tagged union variant for recursive types. Lifetime operations and variant
 * constructors are emitted by TAGGED_UNION_OUT_OF_LINE_IMPL after all variant
 * types have become complete.
 */
#if defined(__clang__)
    #define TAGGED_UNION_OUT_OF_LINE(_name, _def, ...)                         \
        _Pragma("clang diagnostic push");                                      \
        _Pragma("clang diagnostic ignored \"-Wnon-c-typedef-for-linkage\"");   \
        TU_EXP1(_TAGGED_UNION_OUT_OF_LINE(_name, _def, (TU_EXP(__VA_ARGS__)))) \
        _Pragma("clang diagnostic pop");
#else
    #define TAGGED_UNION_OUT_OF_LINE(_name, _def, ...) TU_EXP1(_TAGGED_UNION_OUT_OF_LINE(_name, _def, (TU_EXP(__VA_ARGS__))))
#endif

#define _TAGGED_UNION_OUT_OF_LINE(_name, _def, _variants)                         \
    class _name {                                                                 \
        typedef _name selfT; /*
*/                                                  \
    public:                                                                       \
        TU_TYPEDEFS _variants /*
*/                                                  \
            enum Tag {                                                            \
            TAGDEAD,                                                              \
            TU_TAGS _variants                                                     \
        }; /*
*/                                                                     \
    private:                                                                      \
        Tag tag_;                                                                \
        union DataUnion {                                                         \
            TU_UNION_FIELDS _variants DataUnion() {                               \
            }                                                                     \
            ~DataUnion() {                                                        \
            }                                                                     \
        } mData; /*
*/                                                              \
    public:                                                                       \
        _name();                      /*
*/                                          \
        _name(const _name&) = delete; /*
*/                                          \
        _name(_name&& x) noexcept;    /*
*/                                          \
        _name& operator=(_name&& x);  /*
*/                                          \
        ~_name();                                                                 \
                                                                                  \
        Tag tag() const {                                                         \
            return tag_;                                                         \
        }                                                                         \
        const char* tagStr() const {                                             \
            return tagToStr(tag_);                                             \
        }                                                                         \
        TU_CONSS_DECL(_name, TU_EXP _variants)                                    \
        /*
*/                                                                        \
        static const char* tagToStr(Tag tag) {                                  \
            switch (tag) { /*
*/                                                     \
                case TAGDEAD:                                                     \
                    return "ERR:DEAD";       /*
*/                                   \
                    TU_TOSTR_CASES _variants /*
*/                                   \
            }                                                                     \
            return "";                                                            \
        } /*
*/                                                                      \
        static Tag tagFromStr(const ::std::string& str) {                       \
            if (0)                                                                \
                ;                      /*
*/                                         \
            TU_FROMSTR_CASES _variants /*
*/                                         \
                else throw ::std::runtime_error("enum " #_name " No conversion"); \
        }                                                                         \
    }

#define TAGGED_UNION_OUT_OF_LINE_IMPL(_name, _def, ...) \
    _name::_name()                                      \
        : tag_(TAG_##_def) {                           \
        new (&mData._def) TU_DATANAME(_def)();         \
    }                                                   \
    _name::_name(_name&& x) noexcept                    \
        : tag_(x.tag_) {                              \
        switch (tag_) {                                \
            case TAGDEAD:                               \
                break;                                  \
                TU_MOVE_CASES(TU_EXP(__VA_ARGS__))      \
        }                                               \
        x.tag_ = TAGDEAD;                              \
    }                                                   \
    _name& _name::operator=(_name&& x) {                \
        switch (tag_) {                                \
            case TAGDEAD:                               \
                break;                                  \
                TU_DEST_CASES(TU_EXP(__VA_ARGS__))      \
        }                                               \
        tag_ = x.tag_;                                \
        switch (tag_) {                                \
            case TAGDEAD:                               \
                break;                                  \
                TU_MOVE_CASES(TU_EXP(__VA_ARGS__))      \
        }                                               \
        return *this;                                   \
    }                                                   \
    _name::~_name() {                                   \
        switch (tag_) {                                \
            case TAGDEAD:                               \
                break;                                  \
                TU_DEST_CASES(TU_EXP(__VA_ARGS__))      \
        }                                               \
        tag_ = TAGDEAD;                                \
    }                                                   \
    TU_CONSS_IMPL(_name, TU_EXP(__VA_ARGS__))

/*
*/

namespace {
    template <typename T>
    static void TUDestructInplace(T& v) {
        v.~T();
    }

    template <typename T>
    static void TUMoveInplace(T& dst, T&& src) {
        new (&dst) T(::std::move(src));
    }

    template <typename T>
    static void TUCopyInplace(T& dst, const T& src) {
        new (&dst) T(src);
    }
}
