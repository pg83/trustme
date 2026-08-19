//@ edition: 2024

// From 2024 a binding written `mut` or `ref` may not sit under a default
// binding mode that match ergonomics changed. What this checks is that the
// forms that do not are still accepted.
struct Foo(u8);

fn main() {
    // No implicit borrow: the mode is still by value.
    let Foo(mut owned) = Foo(1);
    owned += 1;
    assert_eq!(owned, 2);

    // An explicit `&` puts the mode back to by value.
    let &Foo(mut from_ref) = &Foo(3);
    from_ref += 1;
    assert_eq!(from_ref, 4);

    // A plain binding under an implicit borrow is fine.
    let Foo(borrowed) = &Foo(5);
    assert_eq!(*borrowed, 5);

    // `ref` where nothing is implicitly borrowing.
    let ref direct = 6u8;
    assert_eq!(*direct, 6);

    // A `&` pattern is fine where nothing is implicitly borrowing: it takes
    // apart the reference it was given.
    let &deref = &7u8;
    assert_eq!(deref, 7);
    let &[&first] = &[&8u8];
    assert_eq!(first, 8);
}
