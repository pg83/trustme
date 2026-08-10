// Extracted from library/core/src/panicking.rs:16
#![allow(unused)]
fn main() {
    fn panic_impl(pi: &core::panic::PanicInfo<'_>) -> !
    { loop {} }
}
