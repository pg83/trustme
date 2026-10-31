#![feature(intrinsics)]
#![allow(internal_features)]

#[rustc_intrinsic]
fn fadd_algebraic<T: Copy>(left: T, right: T) -> T;
#[rustc_intrinsic]
fn fsub_algebraic<T: Copy>(left: T, right: T) -> T;
#[rustc_intrinsic]
fn fmul_algebraic<T: Copy>(left: T, right: T) -> T;
#[rustc_intrinsic]
fn fdiv_algebraic<T: Copy>(left: T, right: T) -> T;
#[rustc_intrinsic]
fn frem_algebraic<T: Copy>(left: T, right: T) -> T;

fn calculate<T: Copy>(left: T, right: T) -> (T, T, T, T, T) {
    (
        fadd_algebraic(left, right),
        fsub_algebraic(left, right),
        fmul_algebraic(left, right),
        fdiv_algebraic(left, right),
        frem_algebraic(left, right),
    )
}

fn main() {
    assert_eq!(calculate(7.5_f32, 2.0), (9.5, 5.5, 15.0, 3.75, 1.5));
    assert_eq!(calculate(-7.5_f64, 2.0), (-5.5, -9.5, -15.0, -3.75, -1.5));
}
