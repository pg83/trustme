//@ compile-fail: Undefined language item 'mrustc-panic_implementation' required
#![no_std]
#![no_main]

#[no_mangle]
extern "C" fn main() -> i32 {
    0
}
