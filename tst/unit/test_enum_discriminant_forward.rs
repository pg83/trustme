// A variant's discriminant may name a variant declared after it.
enum Foo {
    X = 42,
    Y = Foo::X as isize - 3,
}

enum Bar {
    X,
    Y = Bar::X as isize + 2,
}

enum Boo {
    X = Boo::Y as isize * 2,
    Y = 9,
}

enum Chain {
    A = Chain::B as isize + 1,
    B = Chain::C as isize + 1,
    C = 5,
}

fn main() {
    assert_eq!(Foo::X as isize, 42);
    assert_eq!(Foo::Y as isize, 39);
    assert_eq!(Bar::X as isize, 0);
    assert_eq!(Bar::Y as isize, 2);
    assert_eq!(Boo::X as isize, 18);
    assert_eq!(Boo::Y as isize, 9);
    assert_eq!(Chain::A as isize, 7);
    assert_eq!(Chain::B as isize, 6);
    assert_eq!(Chain::C as isize, 5);
}
