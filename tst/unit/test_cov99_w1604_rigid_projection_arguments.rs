//@ compile-fail: error:

trait Left {
    type Arg;
    fn select(&self, value: Self::Arg);
}

trait Right {
    type Arg;
    fn select(&self, value: Self::Arg);
}

trait Blanket {
    fn select(&self, value: ()) {}
}

impl<T> Blanket for T {}

fn probe<T: Left + Right>(value: T) {
    value.select(());
}

fn main() {}
