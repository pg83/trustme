#![feature(transmutability)]
use std::mem::{Assume, TransmuteFrom};
#[repr(C)]
union Wide { whole: u16, narrow: u8 }
fn main() {
    let bytes = [23u8, 29];
    let value = unsafe { <Wide as TransmuteFrom<[u8; 2], { Assume::SAFETY }>>::transmute(bytes) };
    assert_eq!(unsafe { value.whole }, u16::from_ne_bytes(bytes));
}
