#![feature(derive_coerce_pointee)]
use std::marker::CoercePointee;

trait Callback { type Function; fn callback(&self) -> Self::Function; }
struct ReturnsUnit;
struct ReturnsNever;
fn done() {}
impl Callback for ReturnsUnit {
    type Function = fn();
    fn callback(&self) -> fn() { done }
}
impl Callback for ReturnsNever {
    type Function = fn() -> !;
    fn callback(&self) -> fn() -> ! { std::process::abort }
}

#[derive(CoercePointee)]
#[repr(transparent)]
struct UnitHandle<'a, T: ?Sized + Callback<Function = fn()>>(&'a T);
#[derive(CoercePointee)]
#[repr(transparent)]
struct NeverHandle<'a, T: ?Sized + Callback<Function = fn() -> !>>(&'a T);

fn main() {
    let source = ReturnsUnit;
    let handle: UnitHandle<'_, dyn Callback<Function = fn()>> = UnitHandle(&source);
    assert_eq!(handle.0.callback()(), ());
    let source = ReturnsNever;
    let handle: NeverHandle<'_, dyn Callback<Function = fn() -> !>> = NeverHandle(&source);
    assert_eq!(std::mem::size_of_val(&handle.0.callback()), std::mem::size_of::<usize>());
}
