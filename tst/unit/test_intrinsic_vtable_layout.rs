#![feature(ptr_metadata)]
#![allow(dead_code)]

trait Marker {}

#[repr(align(32))]
struct Aligned(u8);

impl Marker for Aligned {}

fn main() {
    let value = Aligned(0);
    let object: &dyn Marker = &value;
    let metadata = core::ptr::metadata(object);

    assert_eq!(metadata.size_of(), 32);
    assert_eq!(metadata.align_of(), 32);
}
