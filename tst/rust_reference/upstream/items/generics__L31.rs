// Extracted from src/items/generics.md:31
#![allow(unused)]
fn main() {
    fn foo<'a, T>() {}
    trait A<U> {}
    struct Ref<'a, T> where T: 'a { r: &'a T }
    struct InnerArray<T, const N: usize>([T; N]);
    struct EitherOrderWorks<const N: bool, U>(U);
}
