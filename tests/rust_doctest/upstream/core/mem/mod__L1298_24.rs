// Extracted from library/core/src/mem/mod.rs:1298
#![allow(unused)]
fn main() {
    use core::mem;
    use core::fmt::Debug;
    #[repr(C)]
    pub struct Struct<T: ?Sized> {
        a: u8,
        b: T,
    }

    #[derive(Debug)]
    #[repr(C, align(4))]
    struct Align4(u32);

    assert_eq!(mem::offset_of!(Struct<dyn Debug>, a), 0); // OK — Sized field
    assert_eq!(mem::offset_of!(Struct<Align4>, b), 4); // OK — not DST

    // assert_eq!(mem::offset_of!(Struct<dyn Debug>, b), 1);
    // ^^^ error[E0277]: ... cannot be known at compilation time

    // To obtain the offset of a !Sized field, examine a concrete value
    // instead of using offset_of!.
    let value: Struct<Align4> = Struct { a: 1, b: Align4(2) };
    let ref_unsized: &Struct<dyn Debug> = &value;
    let offset_of_b = unsafe {
        (&raw const ref_unsized.b).byte_offset_from_unsigned(ref_unsized)
    };
    assert_eq!(offset_of_b, 4);
}
