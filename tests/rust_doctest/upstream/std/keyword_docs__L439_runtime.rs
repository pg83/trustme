// Extracted from library/std/src/keyword_docs.rs:439
#![allow(unused)]
fn main() {
    #[unsafe(no_mangle)]
    pub extern "C" fn callable_from_c(x: i32) -> bool {
        x % 3 == 0
    }
}
