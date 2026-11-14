//@ crate-type: lib
//@ compile-flags: -Z dump-mir

struct Guard;

impl Drop for Guard {
    fn drop(&mut self) {}
}

fn cleanup_only(callback: fn() -> u32, guard: Guard) -> u32 {
    let result = callback();
    core::mem::forget(guard);
    result
}

pub fn run(callback: fn() -> u32) -> u32 {
    cleanup_only(callback, Guard)
}
