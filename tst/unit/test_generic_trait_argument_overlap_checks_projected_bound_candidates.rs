//@ compile-fail: error:

trait Project { type Value; }
trait Bridge<T> {}
trait Pick<U> { fn pick(&self) -> u16; }
struct Slot<T>(T);

impl<T: Project> Pick<T::Value> for Slot<T>
where
    u16: Bridge<T::Value>,
{
    fn pick(&self) -> u16 { 401 }
}
impl<T: Project, U> Pick<U> for Slot<T> {
    fn pick(&self) -> u16 { 409 }
}

fn dispatch<T: Project>(source: Slot<T>) -> u16
where
    u16: Bridge<T::Value>,
{
    source.pick()
}

fn main() {}
