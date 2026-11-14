//@ crate-type: lib
//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort

trait Key {
    type Value;
}

trait Base<T> {
    type Item;
    fn get(&self) -> Self::Item;
}

trait Child<T: Key>: Base<T::Value> {}

fn needs_base<Q: ?Sized + Base<u16, Item = u8>>(value: &Q) -> u8 {
    value.get()
}

fn projected<T: Key<Value = u16>>(value: &dyn Child<T, Item = u8>) -> u8 {
    needs_base(value)
}
