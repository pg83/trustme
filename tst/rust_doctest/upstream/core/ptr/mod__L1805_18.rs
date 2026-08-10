// Extracted from library/core/src/ptr/mod.rs:1805
#![allow(unused)]
fn main() {
    fn read_usize(x: &[u8]) -> usize {
        assert!(x.len() >= size_of::<usize>());

        let ptr = x.as_ptr() as *const usize;

        unsafe { ptr.read_unaligned() }
    }
}
