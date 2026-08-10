use std::mem::{align_of, size_of};

#[repr(packed(4))]
struct RustPacked(u8, f32, i64, u16);

#[repr(C, packed(4))]
struct CPacked(u8, f32, i64, u16);

fn main() {
    assert_eq!(align_of::<RustPacked>(), 4);
    assert_eq!(size_of::<RustPacked>(), 16);
    assert_eq!(align_of::<CPacked>(), 4);
    assert_eq!(size_of::<CPacked>(), 20);
}
