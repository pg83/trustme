// Extracted from library/std/src/keyword_docs.rs:1405
#![allow(unused)]
fn main() {
    trait Example {
        fn example() -> Self;
    }

    struct Foo(i32);

    impl Example for Foo {
        fn example() -> Self {
            Self(42)
        }
    }

    assert_eq!(Foo::example().0, Foo(42).0);
}
