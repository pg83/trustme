//@ compile-fail: conflicting implementations
trait Project {
    type Output;
}

trait Permit<T> {}
trait Select<T> {}
struct Reader;

impl<T: Project> Select<T> for Reader where u32: Permit<T::Output> {}
impl<T: Project> Select<T> for Reader {}

fn main() {}
