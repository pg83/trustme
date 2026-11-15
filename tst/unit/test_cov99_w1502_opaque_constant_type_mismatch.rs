//@ crate-type: lib
//@ compile-fail: Type mismatch

pub trait Shape {
    type Item;
}

const MARK: u8 = 7;

pub fn is_mark<T: Shape>(value: T::Item) -> bool {
    match value {
        MARK => true,
        _ => false,
    }
}
