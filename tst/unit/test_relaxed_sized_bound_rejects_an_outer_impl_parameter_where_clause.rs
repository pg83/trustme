//@ compile-fail: relaxed bounds are only permitted on type parameters
struct Holder<T: ?Sized>(*const T);
impl<T: ?Sized> Holder<T> {
    fn action(&self) where T: ?Sized {}
}
fn main() {}
