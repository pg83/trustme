// Extracted from library/core/src/num/f32.rs:736
#![allow(unused)]
fn main() {
    // f32::EPSILON is the difference between 1.0 and the next number up.
    assert_eq!(1.0f32.next_up(), 1.0 + f32::EPSILON);
    // But not for most numbers.
    assert!(0.1f32.next_up() < 0.1 + f32::EPSILON);
    assert_eq!(16777216f32.next_up(), 16777218.0);
}
