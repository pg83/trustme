#![feature(core_intrinsics, const_heap, adt_const_params, unsized_const_params)]
#![allow(internal_features, incomplete_features)]

const fn allocated_byte(value: u8) -> &'static u8 {
    unsafe {
        let pointer = std::intrinsics::const_allocate(1, 8);
        pointer.write(value);
        &*std::intrinsics::const_make_global(pointer)
    }
}

const FIRST: &u8 = allocated_byte(29);
const SECOND: &u8 = allocated_byte(31);

fn read<const VALUE: &'static u8>() -> u8 {
    *VALUE
}

fn main() {
    assert_eq!(read::<FIRST>(), 29);
    assert_eq!(read::<SECOND>(), 31);
    assert_eq!(FIRST as *const u8 as usize % 8, 0);
    assert_eq!(SECOND as *const u8 as usize % 8, 0);
}
