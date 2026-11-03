//@ compile-variant: -O
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

fn repeat_to_layout<T: Copy>(value: T) -> [T; std::mem::size_of::<T>()]
where
    [(); std::mem::size_of::<T>()]:,
{
    [value; std::mem::size_of::<T>()]
}

fn main() {
    assert_eq!(repeat_to_layout(1123u16), [1123u16; 2]);
    assert_eq!(repeat_to_layout('Д'), ['Д'; 4]);
    assert_eq!(repeat_to_layout([7u8, 11, 13]), [[7u8, 11, 13]; 3]);
}
