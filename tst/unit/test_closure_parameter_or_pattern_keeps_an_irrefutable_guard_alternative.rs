//@ compile-variant: -O
#![feature(guard_patterns)]
#![allow(incomplete_features)]
fn main() {
    let twice = |((value if value < 0) | value): i32| value * 2;
    assert_eq!(twice(-7), -14);
    assert_eq!(twice(11), 22);
}
