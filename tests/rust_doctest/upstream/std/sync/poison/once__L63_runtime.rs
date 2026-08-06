// Extracted from library/std/src/sync/poison/once.rs:63
#![allow(unused)]
fn main() {
    use std::sync::{Once, ONCE_INIT};
    
    static START: Once = ONCE_INIT;
}
