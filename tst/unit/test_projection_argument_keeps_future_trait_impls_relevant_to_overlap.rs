//@ compile-fail: error:

trait Project { type Value; }
trait Bridge<T> {}
trait Pick { fn pick(&self) -> u16; }
struct Slot<T>(T);

impl<T: Project> Pick for Slot<T>
where
    u16: Bridge<T::Value>,
{
    fn pick(&self) -> u16 { 379 }
}
impl<T: Project> Pick for Slot<T> {
    fn pick(&self) -> u16 { 383 }
}

fn main() {}
