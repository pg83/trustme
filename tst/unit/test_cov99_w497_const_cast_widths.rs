//@ crate-type: lib
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

fn narrow<const N: u8>() -> [u8; N as usize] {
    [0; N as usize]
}

fn wide<const N: u64>() -> [u8; N as usize] {
    [0; N as usize]
}

pub fn same_extent() -> [u8; 1] {
    let mut value = narrow::<1>();
    value = wide::<1>();
    value
}
