//@ compile-fail: type annotations needed
fn consume<T: Copy>(_: T) {}
fn main() {
    consume(Default::default());
}
