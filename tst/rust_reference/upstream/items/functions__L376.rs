// Extracted from src/items/functions.md:376
#![allow(unused)]
fn main() {
    fn len(
        #[cfg(windows)] slice: &[u16],
        #[cfg(not(windows))] slice: &[u8],
    ) -> usize {
        slice.len()
    }
}
