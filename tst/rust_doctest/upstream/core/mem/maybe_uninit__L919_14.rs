// Extracted from library/core/src/mem/maybe_uninit.rs:919
#![allow(unused)]
#![feature(maybe_uninit_array_assume_init)]
fn main() {
    use std::mem::MaybeUninit;

    let mut array: [MaybeUninit<i32>; 3] = [MaybeUninit::uninit(); 3];
    array[0].write(0);
    array[1].write(1);
    array[2].write(2);

    // SAFETY: Now safe as we initialised all elements
    let array = unsafe {
        MaybeUninit::array_assume_init(array)
    };

    assert_eq!(array, [0, 1, 2]);
}
