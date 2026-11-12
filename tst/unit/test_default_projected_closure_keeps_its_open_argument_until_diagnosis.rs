//@ compile-fail: error:

#![feature(specialization)]
#![allow(incomplete_features)]

trait Project {
    type Item;
}

struct Receiver<F>(F);

impl<F> Project for Receiver<F> {
    default type Item = u16;
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
    require_missing(project(|_| 587u16));
}
