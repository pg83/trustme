//@ compile-fail: error:0:

struct Wrap<F>(F);

trait Project {
    type Output;
}

impl<F, T> Project for Wrap<F>
where
    F: Fn(u8) -> Option<T>,
{
    type Output = Option<T>;
}

trait Select<F> {}

impl<F, T> Select<F> for fn(u8) -> T
where
    Wrap<F>: Project<Output = T>,
{
}

fn choose<F, S: Select<F>>(_: F) -> S {
    loop {}
}

fn main() {
    let source = |_| Default::default();
    let mut chosen = choose(source);
    chosen = source;
}
