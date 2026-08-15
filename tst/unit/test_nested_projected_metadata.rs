//@ crate-type: lib

trait HasItem {
    type Item;
}

trait Project<T> {
    type Output: ?Sized;
}

struct Source;

impl<T: HasItem> Project<T> for Source {
    type Output = [T::Item];
}

struct Concrete;

impl HasItem for Concrete {
    type Item = u8;
}

struct Tail {
    data: <Source as Project<Concrete>>::Output,
}

impl Tail {}
