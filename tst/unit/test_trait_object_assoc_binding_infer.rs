// `_` written as a trait object's associated-type binding is an inference
// placeholder like any other: it needs a real inference variable allocated
// for it before type checking can resolve it.

trait Restriction {
    type Inner;
}

trait Database: Restriction<Inner = u32> {}

struct Test;

impl Restriction for Test {
    type Inner = u32;
}

impl Database for Test {}

fn observe(_: &dyn Database<Inner = u32>) -> u32 {
    5
}

fn main() {
    let t = Test;
    let implied: &dyn Database<Inner = _> = &t;
    assert_eq!(observe(implied), 5);

    let mut counted = Box::new(0u32..) as Box<dyn Iterator<Item = _>>;
    assert_eq!(counted.next(), Some(0u32));
}
