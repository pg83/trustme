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

trait Missing {}

fn require_missing<F>(_: Receiver<F>)
where
    Receiver<F>: Project,
    <Receiver<F> as Project>::Item: Missing,
{}

fn main() {
    require_missing(Receiver(|_| 593u16));
}
