//@ compile-fail: Type mismatch
trait Shape { type Source; type Target; }
fn invalid<T: Shape>(pointer: T::Source) -> T::Target { pointer }
fn main() {}
