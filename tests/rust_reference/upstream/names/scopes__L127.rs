// Extracted from src/names/scopes.md:127
#![allow(unused)]
fn main() {
    fn example<'a, T, const N: usize>() {
        // Items within functions are allowed to shadow generic parameter in scope.
        fn inner_lifetime<'a>() {} // OK
        fn inner_type<T>() {} // OK
        fn inner_const<const N: usize>() {} // OK
    }
}
