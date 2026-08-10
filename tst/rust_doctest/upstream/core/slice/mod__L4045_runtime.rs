// Extracted from library/core/src/slice/mod.rs:4045
#![allow(unused)]
fn main() {
    unsafe {
        let bytes: [u8; 7] = [1, 2, 3, 4, 5, 6, 7];
        let (prefix, shorts, suffix) = bytes.align_to::<u16>();
        // less_efficient_algorithm_for_bytes(prefix);
        // more_efficient_algorithm_for_aligned_shorts(shorts);
        // less_efficient_algorithm_for_bytes(suffix);
    }
}
