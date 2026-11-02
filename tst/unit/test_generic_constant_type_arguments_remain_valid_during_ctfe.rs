//@ compile-variant: -O
#![feature(generic_const_items)]
const BYTES<T>: usize = core::mem::size_of::<T>();
fn main() { assert_eq!(BYTES::<u16>, 2); assert_eq!(BYTES::<[u8; 3]>, 3); }
