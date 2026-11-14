//@ crate-type: lib

pub trait Project {
    type Item;
}

pub struct Marker;
pub struct Pair<A, B>(pub A, pub B);

impl<U> Pair<U, U> {
    pub fn diagonal(&self) {}
}

impl<A, B> Pair<A, B> {
    pub fn touch(self) {}
}

pub fn probe<T: Project>(value: Pair<Marker, T::Item>) {
    Pair::<Marker, T::Item>::touch(value);
}
