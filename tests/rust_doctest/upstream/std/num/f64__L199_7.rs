// Extracted from library/std/src/num/f64.rs:199
#![allow(unused)]
fn main() {
    let m = 10.0_f64;
    let x = 4.0_f64;
    let b = 60.0_f64;
    
    assert_eq!(m.mul_add(x, b), 100.0);
    assert_eq!(m * x + b, 100.0);
    
    let one_plus_eps = 1.0_f64 + f64::EPSILON;
    let one_minus_eps = 1.0_f64 - f64::EPSILON;
    let minus_one = -1.0_f64;
    
    // The exact result (1 + eps) * (1 - eps) = 1 - eps * eps.
    assert_eq!(one_plus_eps.mul_add(one_minus_eps, minus_one), -f64::EPSILON * f64::EPSILON);
    // Different rounding with the non-fused multiply and add.
    assert_eq!(one_plus_eps * one_minus_eps + minus_one, 0.0);
}
