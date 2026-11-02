//@ compile-variant: -O
#![feature(builtin_syntax)]
#[repr(C)]
struct Record { tag: u8, value: u32, tail: u16 }
fn main() {
    assert_eq!(builtin # offset_of(Record, value), 4);
    assert_eq!(builtin # offset_of(Record, tail), 8);
    assert_eq!(builtin # offset_of((u32, u32), 1), 4);
    assert_eq!(builtin # offset_of((u64, u64), 0), 0);
}
