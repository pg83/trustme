//@ compile-fail: Unknown associated item missing of trait
trait T { fn present(); }
use T::missing;
fn main() {}
