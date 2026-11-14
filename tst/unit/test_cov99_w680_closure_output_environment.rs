trait Factory {
    type Item;
    fn make(self) -> Self::Item;
}

impl<F, T> Factory for F
where
    F: FnOnce() -> T,
{
    type Item = T;

    fn make(self) -> T {
        self()
    }
}

fn materialize<F: Factory>(factory: F) -> F::Item {
    factory.make()
}

fn main() {
    let factory = || Default::default();
    let first: u32 = materialize(factory);
    let second: u32 = materialize(factory);
    assert_eq!((first, second), (0, 0));
}
