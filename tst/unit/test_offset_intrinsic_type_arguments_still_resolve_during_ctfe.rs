//@ compile-variant: -O
#[repr(C)]
struct Pair { head: u8, value: u32 }
fn main() { assert_eq!(core::mem::offset_of!(Pair, value), 4); }
