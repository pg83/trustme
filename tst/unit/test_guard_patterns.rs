// `pat if expr` matches only when the expression holds. The guard belongs to the
// pattern it follows, which is what tells it apart from an arm guard: the `pat`
// macro fragment stops before an arm's `if`, but takes a parenthesised one.
#![feature(guard_patterns)]
#![allow(incomplete_features)]

macro_rules! has_guard {
    ($p:pat) => {
        false
    };
    ($p:pat if $e:expr) => {
        true
    };
}

fn main() {
    assert_eq!(has_guard!(Some(_)), false);
    assert_eq!(has_guard!(Some(_) if true), true);
    assert_eq!(has_guard!((Some(_) if true)), false);

    // A guard may declare locals of its own, and sees the bindings of the
    // pattern it guards.
    let matched = match (0, Some(2)) {
        (_ if { let allow = true; allow }, Some(n) if n % 2 == 0) => true,
        _ => false,
    };
    assert!(matched);
}
