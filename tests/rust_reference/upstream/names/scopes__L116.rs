// Extracted from src/names/scopes.md:116
#![allow(unused)]
fn main() {
    fn example<T>() {
        fn inner(x: T) {} // ERROR: can't use generic parameters from outer function
    }
}
