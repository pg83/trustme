//@ compile-fail: trait bound
trait Parent<const N: usize> {
    type Item;
}

trait Child: Parent<_> {}

trait Seal {}

struct Demand<T: Seal>(T);

fn make() -> Demand<impl Child<Item: Copy>> {
    loop {}
}

fn main() {}
