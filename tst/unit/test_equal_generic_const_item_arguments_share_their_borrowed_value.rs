#![feature(generic_const_items)]
#![allow(incomplete_features)]
const VALUE<const N: usize>: &'static u32 = &(N as u32);
#[inline(never)]
fn from_item<const N: usize>() -> &'static u32 { VALUE::<N> }
fn main() {
    let first = VALUE::<{ 2 + 3 }>;
    let second = VALUE::<5>;
    let third = from_item::<{ 2 + 3 }>();
    assert_eq!(*first, 5);
    assert_eq!(*second, 5);
    assert_eq!(*third, 5);
}
