//@ compile-fail: type annotations needed
//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort

trait Base<T> {
    type Item;
}

trait Child<T, U>: Base<T> + Base<U> {}

fn needs_base<Q: ?Sized + Base<u8, Item = u8>>(_: &Q) {}

fn main() {
    let value: Option<&dyn Child<_, _, Item = u8>> = None;
    needs_base(value.unwrap());
}
