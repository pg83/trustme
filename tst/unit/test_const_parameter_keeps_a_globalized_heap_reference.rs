#![feature(core_intrinsics, const_heap, adt_const_params, unsized_const_params)]
#![allow(internal_features, incomplete_features)]

const fn allocated_byte() -> &'static u8 {
    unsafe {
        let pointer = std::intrinsics::const_allocate(1, 16);
        pointer.write(23);
        &*std::intrinsics::const_make_global(pointer)
    }
}

fn read<const VALUE: &'static u8>() -> u8 {
    *VALUE
}

fn main() {
    assert_eq!(read::<{ allocated_byte() }>(), 23);
}
