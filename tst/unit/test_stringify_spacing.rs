// `stringify!` writes the tokens as they were written, so a space goes between
// two of them only where one is needed to keep them apart. Every pair was
// separated by a space, which turned `a!()` into `a ! ( )`.
//
// Same shape as the upstream tests macros/macro-first-set.rs and
// gccrs torture/builtin_macro_stringify.rs.
macro_rules! a {
    () => {
        "foo"
    };
}

macro_rules! listOf {
    ($($beginning:ident),*; $middle:ident; $($end:ident),*) => {
        stringify!($($beginning,)* $middle $(,$end)*)
    };
}

fn main() {
    // A macro call keeps its name, its `!` and its delimiter together.
    assert_eq!(stringify!(a!()), "a!()");
    let _ = a!();

    // A separator binds to what is on its left.
    assert_eq!(listOf!(a, b; c; d, e), "a, b, c, d, e");
    assert_eq!(listOf!(; f ;), "f");

    // Calls, indexes, fields and paths stay joined.
    assert_eq!(stringify!(f(x)), "f(x)");
    assert_eq!(stringify!(a[0]), "a[0]");
    assert_eq!(stringify!(a.b.c), "a.b.c");
    assert_eq!(stringify!(::std::vec::Vec), "::std::vec::Vec");
    assert_eq!(stringify!(x?), "x?");

    // Operators keep the spaces they need.
    assert_eq!(stringify!(1 + 2), "1 + 2");
    assert_eq!(stringify!(let x = 1;), "let x = 1;");
    assert_eq!(stringify!([1, 2]), "[1, 2]");
}
