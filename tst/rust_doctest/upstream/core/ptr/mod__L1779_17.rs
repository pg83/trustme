// Extracted from library/core/src/ptr/mod.rs:1779
#![allow(unused)]
fn main() {
    #[repr(packed, C)]
    struct Packed {
        _padding: u8,
        unaligned: u32,
    }

    let packed = Packed {
        _padding: 0x00,
        unaligned: 0x01020304,
    };

    // Take the address of a 32-bit integer which is not aligned.
    // In contrast to `&packed.unaligned as *const _`, this has no undefined behavior.
    let unaligned = &raw const packed.unaligned;

    let v = unsafe { std::ptr::read_unaligned(unaligned) };
    assert_eq!(v, 0x01020304);
}
