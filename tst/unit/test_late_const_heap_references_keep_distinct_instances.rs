#![feature(core_intrinsics, const_heap)]
#![allow(internal_features)]

struct Slot<const BYTE: u8>;

impl<const BYTE: u8> Slot<BYTE> {
    const VALUE: &'static u8 = unsafe {
        let pointer = std::intrinsics::const_allocate(1, 16);
        pointer.write(BYTE);
        &*std::intrinsics::const_make_global(pointer)
    };
}

mod nested {
    pub struct Slot<const BYTE: u8>;

    impl<const BYTE: u8> Slot<BYTE> {
        pub const VALUE: &'static u8 = unsafe {
            let pointer = std::intrinsics::const_allocate(1, 8);
            pointer.write(BYTE);
            &*std::intrinsics::const_make_global(pointer)
        };
    }
}

fn main() {
    let first = std::hint::black_box(Slot::<7>::VALUE);
    let second = std::hint::black_box(Slot::<11>::VALUE);
    let third = std::hint::black_box(nested::Slot::<13>::VALUE);
    let fourth = std::hint::black_box(nested::Slot::<17>::VALUE);
    assert_eq!((*first, *second, *third, *fourth), (7, 11, 13, 17));
    assert_eq!(first as *const u8 as usize % 16, 0);
    assert_eq!(second as *const u8 as usize % 16, 0);
    assert_eq!(third as *const u8 as usize % 8, 0);
}
