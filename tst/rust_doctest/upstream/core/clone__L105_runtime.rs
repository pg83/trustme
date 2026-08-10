// Extracted from library/core/src/clone.rs:105
#![allow(unused)]
fn main() {
    #[derive(Copy, Clone)]
    struct Generate<T>(fn() -> T);
}
