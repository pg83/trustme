// Extracted from src/working-with-unsafe.md:29
#![allow(unused)]
fn main() {
    fn index(idx: usize, arr: &[u8]) -> Option<u8> {
        if idx <= arr.len() {
            unsafe {
                Some(*arr.get_unchecked(idx))
            }
        } else {
            None
        }
    }
}
