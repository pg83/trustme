// Extracted from library/std/src/panic.rs:377
#![allow(unused)]
fn main() {
    use std::panic;

    let result = panic::catch_unwind(|| {
        if 1 != 2 {
            panic!("oh no!");
        }
    });

    if let Err(err) = result {
        panic::resume_unwind(err);
    }
}
