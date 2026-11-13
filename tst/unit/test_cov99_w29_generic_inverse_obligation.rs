//@ compile-fail: Failed to find an impl

trait Source {
    type Value;
}

trait Element {
    type Item;
}

impl<S, T> Element for S
where
    S: Source<Value = Vec<T>>,
{
    type Item = T;
}

struct Outer<S>(S);

trait Relay {
    type Value;
}

impl<S: Element> Relay for Outer<S> {
    type Value = S::Item;
}

trait Accept<T> {}

impl<S> Accept<<Outer<S> as Relay>::Value> for Outer<S>
where
    Outer<S>: Relay,
{}

fn require<X: Accept<u8>>(_: &X) {}

fn generic<S: Source>(source: S) {
    require(&Outer(source));
}

fn main() {}
