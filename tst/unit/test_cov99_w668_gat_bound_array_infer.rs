//@ compile-fail: Failed to find an impl of
trait Bound {
    type Item;
}

impl Bound for () {
    type Item = [u8; 1];
}

trait Source {
    type Out<T>: Copy
    where
        T: Bound<Item = [u8; _]>;
}

fn require<T: Copy>() {}

fn probe<S: Source>() {
    require::<S::Out<()>>();
}

fn main() {}
