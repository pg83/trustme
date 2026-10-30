#![feature(core_intrinsics, const_heap)]
#![allow(internal_features)]

pub static ROOT: u8 = 19;

pub fn root_value() -> u8 {
    ROOT
}

pub use crate::root_value as root_alias;

pub mod inner {
    pub const NUMBER: u16 = 5;
    pub static MARKER: u16 = 13;

    struct Slot<const BYTE: u8>;

    impl<const BYTE: u8> Slot<BYTE> {
        const VALUE: &'static u8 = unsafe {
            let pointer = std::intrinsics::const_allocate(1, 1);
            pointer.write(BYTE);
            &*std::intrinsics::const_make_global(pointer)
        };
    }

    pub fn values() -> (u8, u8) {
        let first = std::hint::black_box(Slot::<29>::VALUE);
        let second = std::hint::black_box(Slot::<31>::VALUE);
        (*first, *second)
    }
}

pub use inner::{values as read_alias, MARKER as STATIC_ALIAS, NUMBER as NUMBER_ALIAS};
pub use read_alias as final_read;
