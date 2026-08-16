// An enum discriminant may be another enum's variant cast to an integer. The
// cast read the variant's value before asking for the enum's layout, and it is
// that request which evaluates the discriminants -- so the value came back as
// zero whenever the other enum had not been reached yet.
//
// Same shape as the upstream test enum/issue-23304-2.rs.
#![allow(dead_code)]

enum Base {
    A = 42,
    B = 7,
}

enum Chained {
    A = Base::A as isize,
    B = Base::B as isize,
}

enum Mixed {
    A = Base::A as isize,
    B = 99,
}

enum Later {
    A = 1,
    B = Base::A as isize,
}

enum Narrowed {
    A = Base::A as u8 as isize,
}

fn main() {
    assert_eq!(Base::A as isize, 42);
    assert_eq!(Base::B as isize, 7);

    assert_eq!(Chained::A as isize, 42);
    assert_eq!(Chained::B as isize, 7);

    assert_eq!(Mixed::A as isize, 42);
    assert_eq!(Mixed::B as isize, 99);

    assert_eq!(Later::A as isize, 1);
    assert_eq!(Later::B as isize, 42);

    assert_eq!(Narrowed::A as isize, 42);
}
