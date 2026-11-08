trait Family<P> { type Out<const N: usize>; }
struct Seed;
impl Family<u8> for Seed { type Out<const N: usize> = [u8; N]; }
fn connect<T: Family<u8>, F: FnOnce() -> T>(value: T::Out<3>, make: F) -> T::Out<3> {
    let _ = make();
    value
}
fn forward<T: Family<u8>>(receiver: T, value: T::Out<3>) -> T::Out<3> {
    connect(value, || receiver)
}
fn main() {
    assert_eq!(forward(Seed, [3, 7, 11]), [3, 7, 11]);
}
