trait Source {
    type Value;
}

struct Seed<T>(T);

impl<T> Source for Seed<T> {
    type Value = T;
}

trait Nested {
    type Value;
}

impl<S, U> Nested for S
where
    S: Source<Value = Vec<U>>,
{
    type Value = (U,);
}

struct Outer<S>(S);

trait Relay {
    type Value;
}

impl<S, U> Relay for Outer<S>
where
    S: Source<Value = Vec<U>>,
{
    type Value = (U, <S as Nested>::Value);
}

trait Accept<T> {}

impl<S> Accept<<Outer<S> as Relay>::Value> for Outer<S>
where
    Outer<S>: Relay,
{}

fn require<X: Accept<(u8, (u8,))>>(_: &X) {}

fn main() {
    let value = Outer(Seed(Default::default()));
    require(&value);
}
