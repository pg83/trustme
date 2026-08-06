// Extracted from library/alloc/src/vec/mod.rs:600
use std::alloc::{alloc, Layout};

fn main() {
    let layout = Layout::array::<u32>(16).expect("overflow cannot happen");

    let vec = unsafe {
        let mem = alloc(layout).cast::<u32>();
        if mem.is_null() {
            return;
        }

        mem.write(1_000_000);

        Vec::from_raw_parts(mem, 1, 16)
    };

    assert_eq!(vec, &[1_000_000]);
    assert_eq!(vec.capacity(), 16);
}
