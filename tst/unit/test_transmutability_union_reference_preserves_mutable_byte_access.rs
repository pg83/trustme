#![feature(transmutability)]
use std::mem::{Assume, TransmuteFrom};
#[repr(C)]
union Choice { checked: bool, raw: u8 }
fn main() {
    let mut byte = 43u8;
    let value = unsafe { <&mut Choice as TransmuteFrom<&mut u8, { Assume::SAFETY }>>::transmute(&mut byte) };
    assert_eq!(unsafe { value.raw }, 43);
    value.raw = 47;
    assert_eq!(byte, 47);
}
