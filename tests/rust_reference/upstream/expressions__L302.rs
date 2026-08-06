// Extracted from src/expressions.md:302
#![allow(unused)]
fn main() {
    use core::pin::pin;
    fn temp() {}
    // As above for `format_args!`.
    let _ = pin!({ &temp() }); // OK
}
