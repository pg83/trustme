//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
//@ compile-fail: type annotations needed

trait Base<const N: usize> {
    type Item;
}

trait Child<const N: usize, const M: usize>: Base<N> + Base<M> {}

fn source<const N: usize, const M: usize>() -> &'static dyn Child<N, M, Item = u8> {
    panic!()
}

fn require<const K: usize, T: ?Sized + Base<K, Item = u8>>(_: &T) {}

pub fn probe() {
    let value = source::<_, _>();
    require::<_, _>(value);
}
