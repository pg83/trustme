#pragma once

// Match-side macros for the generated tagged unions (see dev/tu_gen.py and
// the .tu descriptions next to each header). They rely only on the generated
// member surface: `tag()`, `TAG_*`, `is_*`/`as_*`.

#include <cassert>

#define TU_FIRST(a, ...) a
#define TU_EXP1(x) x
#define TU_EXP(...) __VA_ARGS__

// Iteration helpers: apply a macro to every remaining argument.
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
#define TU_GM_I(SUF, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, COUNT, ...) SUF##COUNT
#define TU_GM(SUF, ...) TU_EXP1(TU_GM_I(SUF, __VA_ARGS__, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0))
#define TU_GMO(...) TU_EXP1(TU_GM(TU_DISPO, __VA_ARGS__))
#define TU_GMA(...) TU_EXP1(TU_GM(TU_DISPA, __VA_ARGS__))

// "match"-like statement
// TU_MATCH(Class, m_data, ent, (Variant, CODE), (Variant2, CODE))
#define TU_MATCHA(VARS, NAMES, ...) TU_MATCH(::std::remove_reference<decltype(TU_FIRST VARS)>::type, VARS, NAMES, __VA_ARGS__)
#define TU_MATCH(CLASS, VAR, NAME, ...)                        \
    switch ((TU_FIRST VAR).tag()) { /*
*/                         \
        TU_MATCH_ARMS(CLASS, VAR, NAME, __VA_ARGS__)    /*
*/ \
    }
#define TU_MATCH_DEF(CLASS, VAR, NAME, DEF, ...)               \
    switch ((TU_FIRST VAR).tag()) { /*
*/                         \
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
    brace
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
            brace
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

#define TU_TEST1(VAL, TAG1, TEST) ((VAL).is_##TAG1() && ((VAL).as_##TAG1() TEST))
#define TU_TEST2(VAL, TAG1, FLD1, TAG2, TEST) ((VAL).is_##TAG1() && (VAL).as_##TAG1() FLD1.is_##TAG2() && (VAL).as_##TAG1() FLD1.as_##TAG2() TEST)
#define TU_OPT1(VAL, TAG1, GET) ((VAL).is_##TAG1() ? ((VAL).as_##TAG1() GET) : nullptr)
