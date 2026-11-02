//@ compile-variant: -O
#![feature(core_intrinsics)]
fn main() {
    let mut values = [[2u16, 3], [5, 7]];
    unsafe {
        let view = &mut values[..];
        let row: &mut [u16; 2] = core::intrinsics::slice_get_unchecked(view, std::hint::black_box(1usize));
        row[0] = 11;
        let raw = &mut values[..] as *mut [[u16; 2]];
        let first: *mut [u16; 2] = core::intrinsics::slice_get_unchecked(raw, std::hint::black_box(0usize));
        (*first)[1] = 13;
    }
    assert_eq!(values, [[2, 13], [11, 7]]);
}
