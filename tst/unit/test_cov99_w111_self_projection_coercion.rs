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

struct Choice<F, S: Select<F>>(F, S);

fn main() {
    let source = || Default::default();
    let _ = Choice(source, if true { source } else { || Default::default() });
    let _: Option<u8> = source();
}
