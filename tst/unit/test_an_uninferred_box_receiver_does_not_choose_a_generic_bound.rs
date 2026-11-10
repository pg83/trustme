//@ compile-fail: error
trait Consume {
    fn consume(self: Box<Self>) -> u8;
}

fn ambiguous<T: Consume>() {
    let optional = None;
    let boxed: Box<_> = optional.unwrap();
    let _ = boxed.consume();
}

fn main() {}
