// Extracted from library/alloc/src/vec/mod.rs:710
#![feature(box_vec_non_null)]

use std::alloc::{alloc, Layout};
use std::ptr::NonNull;

fn main() {
    let layout = Layout::array::<u32>(16).expect("overflow cannot happen");

    let vec = unsafe {
        let Some(mem) = NonNull::new(alloc(layout).cast::<u32>()) else {
            return;
        };

        mem.write(1_000_000);

        Vec::from_parts(mem, 1, 16)
    };

    assert_eq!(vec, &[1_000_000]);
    assert_eq!(vec.capacity(), 16);
}
