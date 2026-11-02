//@ compile-fail: Type mismatch
trait Base {}
trait Same<T: ?Sized> {}
impl<T: ?Sized> Same<T> for T {}
fn require<A: ?Sized + Same<B>, B: ?Sized>(_: &A, _: &B) {}
fn invalid(left: &(dyn Base + Send), right: &(dyn Base + Sync)) {
    require(left, right);
}
fn main() {}
