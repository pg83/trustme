// Extracted from src/phantom-data.md:94
#![allow(unused)]
fn main() {
    struct Vec<T> {
        data: *const T, // `*const` for variance!
        len: usize,
        cap: usize,
    }
    
    #[cfg(any())]
    impl<T> Drop for Vec<T> { /* … */ }
}
