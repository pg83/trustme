//@ compile-fail: Type mismatch
#![feature(type_alias_impl_trait)]
type Hidden<T> = impl Fn() -> T;
#[define_opaque(Hidden)]
fn constant<T: Copy>(value: T) -> Hidden<T> { move || value }
fn replace<T>(slot: &mut T, value: T) { *slot = value; }
fn main() {
    let mut value = constant(23u16);
    replace(&mut value, constant(29u32));
}
