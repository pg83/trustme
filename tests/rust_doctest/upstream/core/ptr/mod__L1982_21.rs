// Extracted from library/core/src/ptr/mod.rs:1982
#![allow(unused)]
fn main() {
    #[repr(packed, C)]
    struct Packed {
        _padding: u8,
        unaligned: u32,
    }

    let mut packed: Packed = unsafe { std::mem::zeroed() };

    // Take the address of a 32-bit integer which is not aligned.
    // In contrast to `&packed.unaligned as *mut _`, this has no undefined behavior.
    let unaligned = &raw mut packed.unaligned;

    unsafe { std::ptr::write_unaligned(unaligned, 42) };

    assert_eq!({packed.unaligned}, 42); // `{...}` forces copying the field instead of creating a reference.
}
