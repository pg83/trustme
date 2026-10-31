#![feature(intrinsics)]
#![allow(internal_features)]

#[rustc_intrinsic]
const fn fadd_algebraic<T: Copy>(left: T, right: T) -> T;
#[rustc_intrinsic]
const fn fsub_algebraic<T: Copy>(left: T, right: T) -> T;
#[rustc_intrinsic]
const fn fmul_algebraic<T: Copy>(left: T, right: T) -> T;
#[rustc_intrinsic]
const fn fdiv_algebraic<T: Copy>(left: T, right: T) -> T;
#[rustc_intrinsic]
const fn frem_algebraic<T: Copy>(left: T, right: T) -> T;

const fn calculate<T: Copy>(left: T, right: T) -> (T, T, T, T, T) {
    (
        fadd_algebraic(left, right),
        fsub_algebraic(left, right),
        fmul_algebraic(left, right),
        fdiv_algebraic(left, right),
        frem_algebraic(left, right),
    )
}

const SINGLE: (f32, f32, f32, f32, f32) = calculate(7.5_f32, 2.0);
const DOUBLE: (f64, f64, f64, f64, f64) = calculate(-7.5_f64, 2.0);
fn main() {
    assert_eq!(SINGLE, (9.5, 5.5, 15.0, 3.75, 1.5));
    assert_eq!(DOUBLE, (-5.5, -9.5, -15.0, -3.75, -1.5));
}
