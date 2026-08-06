// Extracted from library/std/src/num/f32.rs:273
#![allow(unused)]
fn main() {
    let a: f32 = 7.0;
    let b = 4.0;
    assert_eq!(a.rem_euclid(b), 3.0);
    assert_eq!((-a).rem_euclid(b), 1.0);
    assert_eq!(a.rem_euclid(-b), 3.0);
    assert_eq!((-a).rem_euclid(-b), 1.0);
    // limitation due to round-off error
    assert!((-f32::EPSILON).rem_euclid(3.0) != 0.0);
}
