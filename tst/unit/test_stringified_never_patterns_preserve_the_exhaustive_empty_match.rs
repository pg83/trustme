//@ compile-fail: No macro arm matched
#![feature(never_patterns)]
enum Empty {}
macro_rules! define {
    ($pattern:pat) => {
        const SPELLING: &str = stringify!($pattern);
        fn impossible(value: Empty) -> u16 { match value { $pattern } }
    };
}
define!(!);
fn main() {
    assert_eq!(SPELLING, "!");
    let _: fn(Empty) -> u16 = impossible;
}
