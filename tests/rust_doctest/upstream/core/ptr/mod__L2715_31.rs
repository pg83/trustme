// Extracted from library/core/src/ptr/mod.rs:2715
#![allow(unused)]
fn main() {
    use std::ptr;

    #[repr(packed)]
    struct Packed {
        f1: u8,
        f2: u16,
    }

    let mut packed = Packed { f1: 1, f2: 2 };
    // `&mut packed.f2` would create an unaligned reference, and thus be Undefined Behavior!
    let raw_f2 = ptr::addr_of_mut!(packed.f2);
    unsafe { raw_f2.write_unaligned(42); }
    assert_eq!({packed.f2}, 42); // `{...}` forces copying the field instead of creating a reference.
}
