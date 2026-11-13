trait Source {
    type Value;
}

struct Seed<T>(T);

impl<T> Source for Seed<T> {
    type Value = T;
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

fn main() {
    let value = Outer(Seed(Default::default()));
    require(&value);
    let _: Outer<Seed<Vec<u8>>> = value;
}
