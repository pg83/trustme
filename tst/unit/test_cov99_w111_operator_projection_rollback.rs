use std::ops::Add;

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

struct Host<F>(F);

impl<F> Add<fn() -> <Wrap<F> as Project>::Output> for Host<F>
where
    Wrap<F>: Project,
{
    type Output = ();

    fn add(self, _: fn() -> <Wrap<F> as Project>::Output) {}
}

fn main() {
    let source = || Default::default();
    let _ = Host(source) + if true { source } else { || Some(7_u8) };
}
