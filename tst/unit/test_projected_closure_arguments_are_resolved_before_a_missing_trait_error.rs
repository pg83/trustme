//@ compile-fail: error:

trait Project {
    type Item;
}

struct Receiver<F>(F);

impl<F: Fn(u16) -> u16> Project for Receiver<F> {
    type Item = u16;
}

fn project<F>(_: F) -> <Receiver<F> as Project>::Item
where
    Receiver<F>: Project,
{
    loop {}
}

trait Missing {}

fn require_missing<T: Missing>(_: T) {}

fn main() {
    require_missing(project(|value| value));
}
