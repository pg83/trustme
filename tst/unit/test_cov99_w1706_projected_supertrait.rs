//@ crate-type: lib

pub trait Family {
    type Item;
}

impl Family for u8 {
    type Item = u16;
}

pub trait Parent<T> {}

pub trait Child<T: Family>: Parent<T::Item> {}

pub fn upcast(value: &dyn Child<u8>) -> &dyn Parent<u16> {
    value
}
