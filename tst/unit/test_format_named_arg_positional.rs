// A named format argument is still an argument: `{}` counts through the
// arguments in the order they were written, and the named ones simply come
// last. Only the arguments actually at the call site are reachable that way --
// the implicit `{ident}` captures are appended afterwards and are not.
//
// Two smaller things from the same parser: a `{ }` fragment is `{}` with
// padding, and a width or precision named argument may start with `_`.
//
// Same shapes as the upstream UI tests macros/issue-98466.rs and
// macros/issue-99907.rs.
fn main() {
    // The single named argument is the first argument, so `{}` picks it up.
    assert_eq!("x is 5", format!("x is {}", x = 5));

    // Positional arguments come first, named ones after, in source order.
    assert_eq!("1 2 7", format!("{} {} {}", 1, 2, y = 7));

    // The same numbering is what an explicit index sees.
    assert_eq!("7 1 2", format!("{2} {0} {1}", 1, 2, y = 7));

    // Referring to it by name gives the same value.
    assert_eq!("7 7", format!("{y} {}", y = 7));

    // Whitespace inside the braces is not part of the argument reference.
    assert_eq!("5", format!("{ }", 5));
    assert_eq!("5", format!("{  }", 5));

    // A width or precision argument named with a leading underscore.
    let _w = 4;
    let _p = 2;
    assert_eq!("ab  ", format!("{:_w$}", "ab", _w = _w));
    assert_eq!("1.25", format!("{:._p$}", 1.25f64, _p = _p));

    // An implicit capture keeps working next to all of that.
    let v = 3;
    assert_eq!("3 9", format!("{v} {}", 9));
}
