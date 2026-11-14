//@ compile-only
//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
//@ compile-fail: type annotations needed

trait Read<const N: usize> {
    fn read(&self) -> usize;
}

trait Pair: Read<3> + Read<5> {}

fn choose<T: Pair>(value: &T) -> usize {
    value.read()
}
