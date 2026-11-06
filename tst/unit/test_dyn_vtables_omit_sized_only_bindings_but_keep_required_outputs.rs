trait Base {
    type Pinned;
    type Hidden where Self: Sized;
}
trait Shape: Base<Pinned = u32> {
    type Live;
    type Left where Self: Sized;
    fn value(&self) -> Self::Live;
}
struct Value(u16);
impl Base for Value {
    type Pinned = u32;
    type Hidden = u64;
}
impl Shape for Value {
    type Live = u16;
    type Left = u8;
    fn value(&self) -> u16 { self.0 }
}

fn main() {
    let value = Value(47);
    let implicit: &dyn Shape<Live = u16> = &value;
    let explicit: &dyn Shape<Live = u16, Left = u8> = &value;
    assert_eq!(implicit.value(), 47);
    assert_eq!(explicit.value(), 47);
}
