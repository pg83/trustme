// A match guard ends at `=>`, so a struct literal is unambiguous there. The
// guard was parsed with the rule that an `if` condition needs — where a `{`
// starts the body — and rejected the literal.
//
// Same shape as the ui test parser/struct-literal-in-match-guard.rs.
#![feature(if_let_guard)]

#[derive(PartialEq)]
struct Foo {
    x: isize,
}

fn describe(f: Foo) -> u8 {
    match () {
        () if f == Foo { x: 42 } => 1,
        () if let Foo { x: 0.. } = Foo { x: 7 } => 2,
        _ => 3,
    }
}

fn main() {
    assert_eq!(describe(Foo { x: 42 }), 1);
    assert_eq!(describe(Foo { x: 1 }), 2);
}
