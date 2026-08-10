// Extracted from library/std/src/thread/mod.rs:334
#![allow(unused)]
fn main() {
    use std::thread;

    let builder = thread::Builder::new().stack_size(32 * 1024);
}
