// `macro_rules` is a legal macro name, so `macro_rules! macro_rules { ... }`
// defines one and a later `macro_rules! {}` calls it. Only the identifier tells
// a definition from a call: with one it is always a definition, without one it
// is a call resolved like any other name.
//
// Same shape as the upstream test macros/user-defined-macro-rules.rs.
macro_rules! macro_rules {
    () => {
        struct S;
    };
    ($n:ident) => {
        struct $n;
    };
}

// No identifier: a call to the macro defined above.
macro_rules! {}

macro_rules! { Other }

// With an identifier it is still a definition, even now that a user macro of
// that name is in scope.
macro_rules! plain {
    () => {
        7
    };
}

fn main() {
    let _s = S;
    let _o = Other;
    assert_eq!(plain!(), 7);
}
