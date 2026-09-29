//@ run-pass
// derive_more's `#[derive(DerefMut)]` on `enum GenericBoxEnum1<T>` with a
// forwarded `Box<T>` field writes `impl<T> DerefMut for GenericBoxEnum1<T>
// where Box<T>: DerefMut`, next to a written `impl<T> Deref` whose
// `type Target = <Box<T> as Deref>::Target`. rustc normalizes an impl's
// associated type where it is used: under the where-clause `Box<T>: Deref`
// is proved by the environment, which says nothing of `Target`, so the
// projection stays as it is on both sides of `deref_mut`. The value had
// been normalized in the `Deref` impl's own environment, to `T`, and the
// method's `<Box<T> as DerefMut>::deref_mut(b)` did not match it.
use std::ops::{Deref, DerefMut};

enum Wrapped<T> {
    Boxed(Box<T>),
}

impl<T> Deref for Wrapped<T> {
    type Target = <Box<T> as Deref>::Target;
    fn deref(&self) -> &Self::Target {
        match self {
            Wrapped::Boxed(b) => <Box<T> as Deref>::deref(b),
        }
    }
}

impl<T> DerefMut for Wrapped<T>
where
    Box<T>: DerefMut,
{
    fn deref_mut(&mut self) -> &mut Self::Target {
        match self {
            Wrapped::Boxed(b) => <Box<T> as DerefMut>::deref_mut(b),
        }
    }
}

fn main() {
    let mut w = Wrapped::Boxed(Box::new(5));
    *w += 1;
    assert_eq!(*w, 6);
}
