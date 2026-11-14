//@ crate-type: lib

pub trait Family {
    type G<T>;
}

pub trait Outer {
    type Item: Family;
}

pub fn preserve<T: Outer>(value: <T::Item as Family>::G<u8>) -> <T::Item as Family>::G<u8>
where
    T::Item: Family,
{
    value
}
