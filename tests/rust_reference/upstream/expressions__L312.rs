// Extracted from src/expressions.md:312
#![allow(unused)]
fn main() {
    use core::pin::pin;
    fn temp() {}
    // The argument is evaluated into a super temporary.
    let x = pin!(temp());
    // The temporary is extended, allowing its use here.
    x; // OK
}
