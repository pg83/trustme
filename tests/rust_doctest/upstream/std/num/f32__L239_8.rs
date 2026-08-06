// Extracted from library/std/src/num/f32.rs:239
#![allow(unused)]
fn main() {
    let a: f32 = 7.0;
    let b = 4.0;
    assert_eq!(a.div_euclid(b), 1.0); // 7.0 > 4.0 * 1.0
    assert_eq!((-a).div_euclid(b), -2.0); // -7.0 >= 4.0 * -2.0
    assert_eq!(a.div_euclid(-b), -1.0); // 7.0 >= -4.0 * -1.0
    assert_eq!((-a).div_euclid(-b), 2.0); // -7.0 >= -4.0 * 2.0
}
