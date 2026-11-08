//@ compile-fail:error
trait Family<P> { type Out<const N: usize>; }
fn connect<T: Family<u8>, F: FnOnce() -> T>(value: T::Out<4>, make: F) -> T::Out<4> {
    let _ = make();
    value
}
fn forward<T: Family<u8>>(receiver: T, value: T::Out<3>) -> T::Out<4> {
    connect(value, || receiver)
}
fn main() {}
