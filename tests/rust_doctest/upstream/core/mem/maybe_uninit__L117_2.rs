// Extracted from library/core/src/mem/maybe_uninit.rs:117
#![allow(unused)]
fn main() {
    use std::mem::{self, MaybeUninit};

    let data = {
        // Create an uninitialized array of `MaybeUninit`.
        let mut data: [MaybeUninit<Vec<u32>>; 1000] = [const { MaybeUninit::uninit() }; 1000];

        // Dropping a `MaybeUninit` does nothing, so if there is a panic during this loop,
        // we have a memory leak, but there is no memory safety issue.
        for elem in &mut data[..] {
            elem.write(vec![42]);
        }

        // Everything is initialized. Transmute the array to the
        // initialized type.
        unsafe { mem::transmute::<_, [Vec<u32>; 1000]>(data) }
    };

    assert_eq!(&data[0], &[42]);
}
