// A macro body written as `expr;` and used where an expression is expected
// keeps the expression -- rustc reports the trailing semicolon as a lint, not an
// error. The leftover semicolon was rejected as an unused token.
//
// Same shape as the upstream test
// lint/semicolon-in-expressions-from-macros/warn-semicolon-in-expressions-from-macros.rs.
#![warn(semicolon_in_expressions_from_macros)]

macro_rules! yes {
    () => {
        true;
    };
}

macro_rules! number {
    () => {
        7;
    };
}

macro_rules! plain {
    () => {
        8
    };
}

fn main() {
    // In a match arm, the value is the expression before the semicolon.
    let v = match true {
        true => false,
        _ => yes!(),
    };
    assert_eq!(v, false);

    let n: u32 = number!();
    assert_eq!(n, 7);

    // Directly in an expression, and nested in a larger one.
    assert_eq!(number!() + 1, 8);
    assert_eq!(plain!(), 8);

    // The statement position is unchanged.
    yes!();
}
