// Extracted from library/std/src/keyword_docs.rs:1376
#![allow(unused)]
fn main() {
    struct Foo(i32);

    impl Foo {
        fn new() -> Self {
            Self(0)
        }
    }

    assert_eq!(Foo::new().0, Foo(0).0);
}
