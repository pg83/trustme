// sqlparser's tests call `<OneOrManyWithParens<_> as Deref>::Target::len(&one)`:
// a path whose self type is a projection with an inferred argument, and whose
// item is an inherent method of what the projection normalizes to (`[T]`).
// Upstream lowers `<X<_> as Deref>::Target` to a fresh inference variable
// constrained by the projection, and resolves `len` once it normalizes.
use std::ops::Deref;

enum OneOrMany<T> {
    One(T),
    Many(Vec<T>),
}

impl<T> Deref for OneOrMany<T> {
    type Target = [T];
    fn deref(&self) -> &[T] {
        match self {
            OneOrMany::One(one) => std::slice::from_ref(one),
            OneOrMany::Many(many) => many,
        }
    }
}

fn main() {
    let one = OneOrMany::One(1u8);
    assert_eq!(<OneOrMany<_> as Deref>::Target::len(&one), 1);
    let many = OneOrMany::Many(vec![2u16, 3]);
    assert_eq!(<OneOrMany<_> as Deref>::Target::len(&many), 2);
    assert_eq!(<OneOrMany<u16> as Deref>::Target::first(&many), Some(&2));
}
