#![feature(transmutability)]
use std::mem::{Assume, TransmuteFrom};
#[repr(C)]
union Payload { whole: u16, narrow: u8 }
#[repr(C, packed)]
struct Frame { tag: u8, payload: Payload }
fn main() {
    let bytes = [31u8, 37, 41];
    let frame = unsafe { <Frame as TransmuteFrom<[u8; 3], { Assume::SAFETY }>>::transmute(bytes) };
    assert_eq!(frame.tag, 31);
    let value = unsafe { std::ptr::addr_of!(frame.payload.whole).read_unaligned() };
    assert_eq!(value, u16::from_ne_bytes([37, 41]));
}
