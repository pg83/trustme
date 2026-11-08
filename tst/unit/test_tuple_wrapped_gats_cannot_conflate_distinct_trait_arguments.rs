//@ compile-fail:mismatch
trait Family<P> { type Out<const N: usize>; }
fn connect<T: Family<u8>, F: FnOnce() -> T>(value: (<T as Family<u8>>::Out<3>,), make: F)
    -> (<T as Family<u8>>::Out<3>,)
{
    let _ = make();
    value
}
fn forward<T: Family<u8> + Family<u16>>(receiver: T, value: (<T as Family<u16>>::Out<3>,))
    -> (<T as Family<u8>>::Out<3>,)
{
    connect(value, || receiver)
}
fn main() {}
