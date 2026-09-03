// An impl whose self type is a projection belongs to the named type that the
// projection normalizes to. Trait markings (here: "this type has a Deref
// impl", which is what lets autoderef run at all) have to be recorded against
// that named type, so the projection must be normalized before the markings
// pass looks the type item up.

use std::ops::Deref;

trait Id {
    type Assoc;
}

impl<T> Id for T {
    type Assoc = T;
}

trait Foo {
    fn foo(&self) -> u32;
}

// Normalizes to a type with no named type item at all.
impl Foo for <() as Id>::Assoc {
    fn foo(&self) -> u32 {
        7
    }
}

struct LocalTy;

trait Marker {}

// Normalizes to a local struct.
impl Marker for <LocalTy as Id>::Assoc {}

fn takes<T: Marker>(_: T) -> u32 {
    11
}

struct Wrapper(u32);

impl Deref for <Wrapper as Id>::Assoc {
    type Target = u32;

    fn deref(&self) -> &u32 {
        &self.0
    }
}

fn main() {
    assert_eq!(().foo(), 7);
    assert_eq!(takes(LocalTy), 11);
    assert_eq!(*Wrapper(9), 9);
}
