// Extracted from src/ffi.md:858
use std::panic::catch_unwind;

#[unsafe(no_mangle)]
pub extern "C" fn oh_no() -> i32 {
    let result = catch_unwind(|| {
        panic!("Oops!");
    });
    match result {
        Ok(_) => 0,
        Err(_) => 1,
    }
}

fn main() {}
