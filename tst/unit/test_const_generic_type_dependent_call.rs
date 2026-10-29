#![feature(generic_const_exprs)]
const fn read<const N: usize>() -> usize { N }
const fn width<T>() -> usize where [(); std::mem::size_of::<T>()]: {
    read::<{ std::mem::size_of::<T>() }>()
}
const SMALL: usize = width::<u16>();
const PAIR: usize = width::<(u64, u64)>();
const ARRAY: usize = width::<[u16; 3]>();
const OBJECT: usize = width::<&dyn std::fmt::Display>();
fn main() {
    assert_eq!(SMALL, 2); assert_eq!(PAIR, 16);
    assert_eq!(ARRAY, 6); assert_eq!(OBJECT, 2 * std::mem::size_of::<usize>());
}
