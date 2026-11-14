//@ crate-type: lib
//@ compile-flags: --emit=metadata

trait Family {
    type Item<T>;
}

impl Family for () {
    type Item<T> = T;
}

pub fn associated_bound<T>()
where
    (): Family<Item<T>: Copy>,
{}
