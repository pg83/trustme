//@ compile-fail: error:
#![feature(inherent_associated_types)]

struct Wrapped<F>(F);

impl<F> Wrapped<F> {
    type Item = F;
}

struct Factory;

impl Factory {
    fn make() -> Wrapped<impl Fn() -> u16>::Item {
        || 197
    }
}

fn main() {
    let _ = Factory::make();
}
