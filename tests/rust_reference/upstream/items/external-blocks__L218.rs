// Extracted from src/items/external-blocks.md:218
#![allow(unused)]
fn main() {
    unsafe extern "C" {
        unsafe fn foo(...);
        unsafe fn bar(x: i32, ...);
        unsafe fn with_name(format: *const u8, args: ...);
        // SAFETY: This function guarantees it will not access
        // variadic arguments.
        safe fn ignores_variadic_arguments(x: i32, ...);
    }
}
