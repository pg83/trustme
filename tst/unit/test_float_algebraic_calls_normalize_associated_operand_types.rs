#![feature(intrinsics)]
#![allow(internal_features)]

#[rustc_intrinsic]
fn fadd_algebraic<T: Copy>(left: T, right: T) -> T;

trait Format { type Value: Copy; }
struct Single;
struct Double;
impl Format for Single { type Value = f32; }
impl Format for Double { type Value = f64; }
fn sum<F: Format>(left: F::Value, right: F::Value) -> F::Value {
    fadd_algebraic(left, right)
}
fn main() {
    assert_eq!(sum::<Single>(1.25, 2.5), 3.75);
    assert_eq!(sum::<Double>(7.25, 0.5), 7.75);
}
