trait Project { type Value; }
trait Bridge<T> {}
trait Pick { fn pick(&self) -> u16; }
struct Slot<T>(T);
struct Leaf;
impl Project for Leaf { type Value = u8; }

impl<T: Project> Pick for Slot<T>
where
    u16: Bridge<T::Value>,
{
    fn pick(&self) -> u16 { 389 }
}
impl Pick for Slot<Leaf> {
    fn pick(&self) -> u16 { 397 }
}

fn main() {
    assert_eq!(Slot(Leaf).pick(), 397);
}
