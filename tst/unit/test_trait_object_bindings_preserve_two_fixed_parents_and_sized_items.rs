trait Prefix { type First; }
trait Suffix {
    type Last;
    type Private where Self: Sized;
}
trait Channel: Prefix<First = u16> + Suffix<Last = u32> {
    type Current;
    fn read(&self) -> Self::Current;
}

struct Wire(u8);
impl Prefix for Wire { type First = u16; }
impl Suffix for Wire {
    type Last = u32;
    type Private = String;
}
impl Channel for Wire {
    type Current = u8;
    fn read(&self) -> u8 { self.0 }
}

fn main() {
    let channel: Box<dyn Channel<Current = _>> = Box::new(Wire(53));
    assert_eq!(channel.read(), 53);
}
