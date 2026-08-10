// Extracted from src/attributes/diagnostics.md:256
#![allow(unused)]
fn main() {
    unsafe fn an_unsafe_fn() {}
    // The order of these two attributes does not matter.
    #[deny(warnings)]
    // The unsafe_code lint is normally "allow" by default.
    #[warn(unsafe_code)]
    fn example_err() {
        // This is an error because the `unsafe_code` warning has
        // been lifted to "deny".
        unsafe { an_unsafe_fn() } // ERROR: use of `unsafe` block
    }
}
