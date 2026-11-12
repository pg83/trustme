//@ compile-fail: No macro arm matched
#![feature(never_patterns)]

macro_rules! pattern_text {
    ($pattern:pat) => {
        stringify!($pattern)
    };
}

fn main() {
    assert_eq!(pattern_text!(!), "!");
    assert_eq!(pattern_text!((!,)), "(!,)");
    assert_eq!(pattern_text!(Some(!)), "Some(!)");
}
