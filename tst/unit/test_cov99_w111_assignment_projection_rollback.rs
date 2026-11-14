//@ compile-fail: error:0:

struct Wrap<F>(F);

trait Project {
    type Output;
}

impl<F, T> Project for Wrap<F>
where
    F: Fn() -> Option<T>,
{
    type Output = Option<T>;
}

trait Select<F> {}

impl<F> Select<F> for fn() -> <Wrap<F> as Project>::Output
where
    Wrap<F>: Project,
{
}

fn choose<F, S: Select<F>>(_: F) -> S {
    loop {}
}

fn main() {
    let source = || Default::default();
    let mut chosen = choose(source);
    chosen = source;
}
