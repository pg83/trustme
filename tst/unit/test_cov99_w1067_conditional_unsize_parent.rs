//@ compile-fail: Child

trait Parent {}

trait Child: Parent {
    fn make() -> &'static Self;
}

fn main() {
    let mut value = <_ as Child>::make();
    value = if true { &7u8 } else { &8u8 };
    let _ = value;
}
