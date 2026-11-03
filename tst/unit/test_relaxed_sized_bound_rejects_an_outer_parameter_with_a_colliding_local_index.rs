//@ compile-fail: relaxed bounds are only permitted on type parameters
struct Holder<T>(*const T);
impl<T> Holder<T> {
    fn action<U>(&self, _: U) where T: ?Sized {}
}
fn main() {}
