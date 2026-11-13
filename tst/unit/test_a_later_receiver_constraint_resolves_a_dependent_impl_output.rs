trait Source {
    type Item;
}

#[derive(Default)]
struct Origin;

impl Source for Origin {
    type Item = u16;
}

trait Gate<T> {}
impl Gate<u16> for () {}

struct Wrapper<T>(T);

trait Select {
    type Output;
    fn select(&self) -> Self::Output;
}

impl<T, U> Select for Wrapper<T>
where
    T: Source<Item = U>,
    (): Gate<U>,
    U: Default,
{
    type Output = U;

    fn select(&self) -> U {
        U::default()
    }
}

fn make<T: Default>() -> Wrapper<T> {
    Wrapper(T::default())
}

fn constrain(_: &Wrapper<Origin>) {}

fn main() {
    let value = make();
    let output = value.select();
    constrain(&value);
    assert_eq!(output, 0u16);
}
