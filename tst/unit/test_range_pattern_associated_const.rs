//@ run-pass
// An associated constant used as a range-pattern bound reads the value the
// impl gives it. The binding HIR conversion records is the trait's
// declaration, which has no value of its own, so the bound used to read zero
// and the range came out empty or far too wide.

struct Foo;

trait HasNum {
    const LOW: isize;
    const HIGH: isize;
}

impl HasNum for Foo {
    const LOW: isize = 1;
    const HIGH: isize = 3;
}

impl Foo {
    const INHERENT: isize = 5;
}

fn in_trait_range(x: isize) -> bool {
    match x {
        <Foo as HasNum>::LOW..=<Foo as HasNum>::HIGH => true,
        _ => false,
    }
}

fn main() {
    assert!(in_trait_range(1));
    assert!(in_trait_range(3));
    assert!(!in_trait_range(0));
    assert!(!in_trait_range(4));

    assert!(match 1 {
        1..=<Foo as HasNum>::LOW => true,
        _ => false,
    });
    assert!(match 2 {
        1..=<Foo as HasNum>::LOW => true,
        _ => false,
    } == false);

    assert!(match 5 {
        ..=<Foo>::INHERENT => true,
        _ => false,
    });
    assert!(match 6 {
        ..=<Foo>::INHERENT => true,
        _ => false,
    } == false);
    assert!(match 5 {
        <Foo>::INHERENT.. => true,
        _ => false,
    });

    assert!(match <Foo as HasNum>::HIGH {
        <Foo as HasNum>::HIGH => true,
        _ => false,
    });
}
