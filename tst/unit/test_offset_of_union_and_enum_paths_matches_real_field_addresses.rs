#![feature(offset_of_enum)]
use std::mem::offset_of;

#[repr(C)]
#[derive(Clone, Copy)]
struct Nested(u8, u32);
#[repr(C)]
union Storage { bytes: [u8; 8], word: u64, nested: Nested }
#[repr(C, u8)]
enum Choice { First { byte: u8, word: u32 }, Second(u16, u64) }

fn main() {
    assert_eq!(offset_of!(Storage, word), 0);
    assert_eq!(offset_of!(Storage, nested.1), offset_of!(Nested, 1));
    let first = Choice::First { byte: 3, word: 17 };
    let base = &first as *const _ as usize;
    let field = match &first { Choice::First { word, .. } => word as *const u32 as usize, _ => unreachable!() };
    assert_eq!(offset_of!(Choice, First.word), field - base);
    let second = Choice::Second(19, 23);
    let base = &second as *const _ as usize;
    let field = match &second { Choice::Second(_, word) => word as *const u64 as usize, _ => unreachable!() };
    assert_eq!(offset_of!(Choice, Second.1), field - base);
}
