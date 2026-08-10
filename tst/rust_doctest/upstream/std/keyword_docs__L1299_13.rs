// Extracted from library/std/src/keyword_docs.rs:1299
#![allow(unused)]
fn main() {
    struct Foo(i32);

    impl Foo {
        // No `self`.
        fn new() -> Self {
            Self(0)
        }

        // Consuming `self`.
        fn consume(self) -> Self {
            Self(self.0 + 1)
        }

        // Borrowing `self`.
        fn borrow(&self) -> &i32 {
            &self.0
        }

        // Borrowing `self` mutably.
        fn borrow_mut(&mut self) -> &mut i32 {
            &mut self.0
        }
    }

    // This method must be called with a `Type::` prefix.
    let foo = Foo::new();
    assert_eq!(foo.0, 0);

    // Those two calls produces the same result.
    let foo = Foo::consume(foo);
    assert_eq!(foo.0, 1);
    let foo = foo.consume();
    assert_eq!(foo.0, 2);

    // Borrowing is handled automatically with the second syntax.
    let borrow_1 = Foo::borrow(&foo);
    let borrow_2 = foo.borrow();
    assert_eq!(borrow_1, borrow_2);

    // Borrowing mutably is handled automatically too with the second syntax.
    let mut foo = Foo::new();
    *Foo::borrow_mut(&mut foo) += 1;
    assert_eq!(foo.0, 1);
    *foo.borrow_mut() += 1;
    assert_eq!(foo.0, 2);
}
