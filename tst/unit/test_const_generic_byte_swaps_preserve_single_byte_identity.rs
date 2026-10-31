//@ compile-flags: -Zmir-opt-level=0
#![feature(core_intrinsics)]
const fn swapped<T: Copy>(value: T) -> T { std::intrinsics::bswap(value) }
const VALUES: (u8, i8) = (swapped(0x93u8), swapped(-19i8));
fn main() { assert_eq!(VALUES, (0x93, -19)); }
