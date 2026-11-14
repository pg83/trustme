//@ crate-type: lib
//@ compile-flags: -Z mir-opt-level=3 -Z dump-mir
#![no_std]

struct Guard;

impl Drop for Guard {
    fn drop(&mut self) {}
}

fn callback_once(callback: fn()) {
    callback();
}

fn return_guard(callback: fn(), guard: Guard) -> Guard {
    callback_once(callback);
    guard
}

pub fn run(callback: fn()) {
    let guard = return_guard(callback, Guard);
    core::mem::forget(guard);
}
