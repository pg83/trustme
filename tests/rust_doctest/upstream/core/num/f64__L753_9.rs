// Extracted from library/core/src/num/f64.rs:753
#![allow(unused)]
fn main() {
    // f64::EPSILON is the difference between 1.0 and the next number up.
    assert_eq!(1.0f64.next_up(), 1.0 + f64::EPSILON);
    // But not for most numbers.
    assert!(0.1f64.next_up() < 0.1 + f64::EPSILON);
    assert_eq!(9007199254740992f64.next_up(), 9007199254740994.0);
}
