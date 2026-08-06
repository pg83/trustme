// Extracted from library/std/src/num/f64.rs:273
#![allow(unused)]
fn main() {
    let a: f64 = 7.0;
    let b = 4.0;
    assert_eq!(a.rem_euclid(b), 3.0);
    assert_eq!((-a).rem_euclid(b), 1.0);
    assert_eq!(a.rem_euclid(-b), 3.0);
    assert_eq!((-a).rem_euclid(-b), 1.0);
    // limitation due to round-off error
    assert!((-f64::EPSILON).rem_euclid(3.0) != 0.0);
}
