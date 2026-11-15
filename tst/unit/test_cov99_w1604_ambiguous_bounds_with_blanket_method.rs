//@ compile-fail: error:

trait Choice {}
impl Choice for u8 {}
impl Choice for u16 {}

trait Left {
    fn select<C: Choice>(&self);
}

trait Right {
    fn select<C: Choice>(&self);
}

trait Blanket {
    fn select(&self) {}
}

impl<T> Blanket for T {}

fn probe<T: Left + Right>(value: T) {
    value.select();
}

fn main() {}
