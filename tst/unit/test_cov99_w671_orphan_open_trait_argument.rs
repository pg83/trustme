//@ crate-type: lib
//@ compile-fail: conflicting implementations

trait Gate<T> {}
trait Overlap {}
struct Wrap<T>(T);

impl<T> Overlap for Wrap<T> where u8: Gate<T> {}
impl<T> Overlap for Wrap<T> {}
