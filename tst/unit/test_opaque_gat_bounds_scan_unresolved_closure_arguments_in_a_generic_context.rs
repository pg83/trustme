//@ compile-fail: error:

trait Project {
    type Item<F>;
}

struct Receiver<T>(T);

trait Missing {}

fn require_missing<T, F>(_: F)
where
    Receiver<T>: Project,
    <Receiver<T> as Project>::Item<F>: Missing,
{}

fn check<T>()
where
    Receiver<T>: Project,
{
    require_missing::<T, _>(|known: u16, _| known);
}

fn main() {}
