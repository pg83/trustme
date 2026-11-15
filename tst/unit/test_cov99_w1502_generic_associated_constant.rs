//@ crate-type: lib
//@ compile-fail: constant patterns cannot depend on generic parameters:

pub trait Shape {
    type Item;
    const MARK: Self::Item;
}

pub fn is_mark<T: Shape>(value: T::Item) -> bool {
    match value {
        T::MARK => true,
        _ => false,
    }
}
