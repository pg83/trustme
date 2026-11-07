trait Factory<Output> {
    fn make(self) -> Output;
}

impl<F, Output> Factory<Output> for F
where
    F: FnOnce() -> Output,
{
    fn make(self) -> Output {
        self()
    }
}

fn main() {
    let factory = || |value| value;
    let identity = factory.make();
    assert_eq!(identity(677u16), 677u16);
}
