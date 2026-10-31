#![feature(intrinsics)]
#![allow(internal_features)]

#[rustc_intrinsic]
fn fmul_algebraic<T: Copy>(_left: T, _right: T) -> T {
    panic!("the floating intrinsic fallback body ran")
}
fn main() {
    assert_eq!(fmul_algebraic(1.5_f32, 4.0), 6.0);
    assert_eq!(fmul_algebraic(-2.5_f64, 3.0), -7.5);
}
