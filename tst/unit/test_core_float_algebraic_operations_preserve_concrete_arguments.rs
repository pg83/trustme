#![feature(core_intrinsics, float_algebraic)]
#![allow(internal_features)]
fn main() {
    use core::intrinsics::{fadd_algebraic, fsub_algebraic, fmul_algebraic, fdiv_algebraic, frem_algebraic};
    assert_eq!(fadd_algebraic(1.25_f32, 2.5), 3.75);
    assert_eq!(fsub_algebraic(7.25_f64, 0.5), 6.75);
    assert_eq!(fmul_algebraic(1.5_f32, 4.0), 6.0);
    assert_eq!(fdiv_algebraic(7.5_f64, 2.0), 3.75);
    assert_eq!(frem_algebraic(-7.5_f32, 2.0), -1.5);
}
