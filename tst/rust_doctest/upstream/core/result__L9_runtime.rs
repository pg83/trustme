// Extracted from library/core/src/result.rs:9
#![allow(unused)]
fn main() {
    #[allow(dead_code)]
    enum Result<T, E> {
       Ok(T),
       Err(E),
    }
}
