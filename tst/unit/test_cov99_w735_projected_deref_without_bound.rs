//@ compile-fail: Deref
trait Family {
    type Item;
}

fn read<T: Family>(value: T::Item) -> u8 {
    *value
}

fn main() {}
