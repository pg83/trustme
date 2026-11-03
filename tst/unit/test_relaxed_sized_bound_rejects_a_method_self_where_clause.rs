//@ compile-fail: relaxed bounds are only permitted on type parameters
trait Invalid {
    fn action(&self) where Self: ?Sized;
}
fn main() {}
