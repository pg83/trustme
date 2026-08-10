use std::mem::{align_of, size_of};

#[repr(C)]
enum Value {
    A(u8, u16, u8),
    B(u8, u16, u8),
}

fn main() {
    assert_eq!(align_of::<Value>(), 4);
    assert_eq!(size_of::<Value>(), 12);
}
