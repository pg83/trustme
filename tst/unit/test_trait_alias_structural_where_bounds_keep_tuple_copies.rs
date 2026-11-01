#![feature(trait_alias)]
trait Wrapped<A, B> = Sized where
    (A, B): Copy, [A; 2]: Copy, [B; 3]: Copy,
    *const A: Copy, *mut B: Copy, fn(A) -> B: Copy;
fn duplicate<A, B, V: Wrapped<A, B>>(value: (A, B)) -> ((A, B), (A, B)) { (value, value) }
fn main() { assert_eq!(duplicate::<u8, u16, ()>((7, 300)), ((7, 300), (7, 300))); }
