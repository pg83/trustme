//@ compile-fail: No applicable methods
#![feature(type_alias_impl_trait)]

struct Pair<T, U>(T, U);
impl<T> Pair<T, T> {
    fn width(&self) -> usize { std::mem::size_of::<T>() }
}

fn opaque<T: Copy>(value: T) -> impl Copy { value }
type Hidden<T: Copy> = impl Copy;
#[define_opaque(Hidden)]
fn hidden<T: Copy>(value: T) -> Hidden<T> { value }

fn check(value: Pair<Hidden<u8>, Hidden<u16>>) {
    let _ = value.width();
}
fn main() {}
