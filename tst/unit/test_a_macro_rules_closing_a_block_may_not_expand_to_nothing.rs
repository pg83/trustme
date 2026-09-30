//@ compile-fail: Macro didn't expand to anything
// A `macro_rules!` call closing a block is made into an expression (rustc's
// `make_from` asks the expansion for `make_expr`), and an empty expansion
// is an error there - "macro expansion ends with an incomplete expression".
macro_rules! nothing {
    () => {};
}

fn closes_with_nothing() {
    nothing!()
}

fn main() {
    closes_with_nothing();
}
