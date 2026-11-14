//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort
//@ compile-fail: Cov684Loop

struct Host;
trait Cov684Loop<T> {
    type Item;
}
impl<T> Cov684Loop<T> for Host
where
    Host: Cov684Loop<T, Item = ()>,
{
    type Item = ();
}
fn demand<T, U>() where Host: Cov684Loop<T, Item = U> {}
fn main() {
    demand::<_, _>();
}
