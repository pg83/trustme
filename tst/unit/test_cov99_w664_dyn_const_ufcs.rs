//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort

trait Tag<const N: usize> {}
impl Tag<3> for () {}

trait Count {
    fn count(&self) -> usize;
}

impl<T: ?Sized> Count for T {
    fn count(&self) -> usize {
        1
    }
}

fn main() {
    let object: &dyn Tag<3> = &();
    let count = <dyn Tag<_>>::count(object);
    assert_eq!(count, 1);
}
