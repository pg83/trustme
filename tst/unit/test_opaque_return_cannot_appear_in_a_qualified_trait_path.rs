//@ compile-fail: error:

trait Extract<F> {
    type Item;
}

struct Wrapped;

impl<F> Extract<F> for Wrapped {
    type Item = F;
}

struct Factory;

impl Factory {
    fn make() -> <Wrapped as Extract<impl Fn() -> u16>>::Item {
        || 211
    }
}

fn main() {
    let _ = Factory::make();
}
