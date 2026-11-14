trait Factory {
    type Item;
}

impl<F, T> Factory for F
where
    F: FnOnce() -> T,
{
    type Item = T;
}

fn inspect<F: Factory>(_: &F)
where
    F::Item: Copy,
{
}

fn main() {
    let factory = || Default::default();
    inspect(&factory);
    inspect(&factory);
    let result: u32 = factory();
    assert_eq!(result, 0);
}
