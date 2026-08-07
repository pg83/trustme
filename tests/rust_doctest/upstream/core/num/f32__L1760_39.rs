// Extracted from library/core/src/num/f32.rs:1760
#![allow(unused)]
#![feature(core_float_math)]
fn main() {

    // FIXME(#140515): mingw has an incorrect fma
    // https://sourceforge.net/p/mingw-w64/bugs/848/
    #[cfg(all(target_os = "windows", target_env = "gnu", not(target_abi = "llvm")))] {
    use core::f32;

    let m = 10.0_f32;
    let x = 4.0_f32;
    let b = 60.0_f32;

    assert_eq!(f32::math::mul_add(m, x, b), 100.0);
    assert_eq!(m * x + b, 100.0);

    let one_plus_eps = 1.0_f32 + f32::EPSILON;
    let one_minus_eps = 1.0_f32 - f32::EPSILON;
    let minus_one = -1.0_f32;

    // The exact result (1 + eps) * (1 - eps) = 1 - eps * eps.
    assert_eq!(
        f32::math::mul_add(one_plus_eps, one_minus_eps, minus_one),
        -f32::EPSILON * f32::EPSILON
    );
    // Different rounding with the non-fused multiply and add.
    assert_eq!(one_plus_eps * one_minus_eps + minus_one, 0.0);
    }
}
