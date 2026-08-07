use std::mem::{align_of, size_of};

#[repr(align(4))]
#[repr(align(16))]
#[repr(align(8))]
struct AlignedStruct(i32);

#[repr(align(1), align(16))]
#[repr(align(32))]
#[repr(align(4))]
enum AlignedEnum {
    A,
    B,
}

fn main() {
    assert_eq!((align_of::<AlignedStruct>(), size_of::<AlignedStruct>()), (16, 16));
    assert_eq!((align_of::<AlignedEnum>(), size_of::<AlignedEnum>()), (32, 32));
}
