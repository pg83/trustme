// Extracted from src/scope/lifetime/static_lifetime.md:7
#![allow(unused)]
fn main() {
    // A reference with 'static lifetime:
    let s: &'static str = "hello world";
    
    // 'static as part of a trait bound:
    fn generic<T>(x: T) where T: 'static {}
}
