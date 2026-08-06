// Extracted from src/names/scopes.md:136
#![allow(unused)]
fn main() {
    trait SomeTrait<'a, T, const N: usize> {
        fn example_lifetime<'a>() {} // ERROR: 'a is already in use
        fn example_type<T>() {} // ERROR: T is already in use
        fn example_const<const N: usize>() {} // ERROR: N is already in use
        fn example_mixed<const T: usize>() {} // ERROR: T is already in use
    }
}
