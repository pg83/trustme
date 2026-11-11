//@ compile-fail: relaxed bounds
trait Invalid where Self: ?Sized {}
fn main() {}
