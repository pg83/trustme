// A `#![no_main]` crate supplies the entry point itself, as a `#[no_mangle]
// extern "C" fn main`. The C emitter renamed it to `main` with a `#define`,
// which handed C++ a `main` whose second parameter is not `char**` -- a
// signature C++ rejects outright. The function keeps its own name now, and a
// real `main` calls it.
#![no_main]

use std::os::raw::{c_char, c_int};

static mut SEEN_ARGC: c_int = 0;

#[unsafe(no_mangle)]
pub extern "C" fn main(argc: c_int, argv: *const *const c_char) -> c_int {
    unsafe {
        SEEN_ARGC = argc;
    }

    // The program is run with no extra arguments, so there is exactly one.
    assert!(argc >= 1);
    assert!(!argv.is_null());

    // The first argument is a valid C string.
    let first = unsafe { *argv };
    assert!(!first.is_null());

    let seen = unsafe { core::ptr::read(&raw const SEEN_ARGC) };
    assert_eq!(seen, argc);
    0
}
