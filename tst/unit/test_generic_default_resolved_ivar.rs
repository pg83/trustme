use std::marker::PhantomData;

struct Wrapper<T, S = T> {
    value: T,
    marker: PhantomData<S>,
}

fn make() -> Wrapper<impl Clone> {
    Wrapper {
        value: (),
        marker: PhantomData,
    }
}

fn main() {
    let _ = make();
}
