// Extracted from library/core/src/ptr/mod.rs:2643
#![allow(unused)]
fn main() {
    use std::ptr;

    #[repr(packed)]
    struct Packed {
        f1: u8,
        f2: u16,
    }

    let packed = Packed { f1: 1, f2: 2 };
    // `&packed.f2` would create an unaligned reference, and thus be Undefined Behavior!
    let raw_f2 = ptr::addr_of!(packed.f2);
    assert_eq!(unsafe { raw_f2.read_unaligned() }, 2);
}
