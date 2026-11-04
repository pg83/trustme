#![feature(transmutability)]
use std::mem::{Assume, TransmuteFrom};
#[repr(C)]
union Choice { checked: bool, raw: u8 }
fn main() {
    for byte in [0u8, 1, 17, 255] {
        let value = unsafe { <Choice as TransmuteFrom<u8, { Assume::SAFETY }>>::transmute(byte) };
        assert_eq!(unsafe { value.raw }, byte);
        let back = unsafe { <u8 as TransmuteFrom<Choice, { Assume::SAFETY }>>::transmute(value) };
        assert_eq!(back, byte);
    }
}
