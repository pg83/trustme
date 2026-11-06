trait Parent {
    type Item;
    fn value(&self) -> Self::Item;
}
trait Root: Parent<Item = u16> {}
struct Value(u16);
impl Parent for Value {
    type Item = u16;
    fn value(&self) -> u16 { self.0 }
}
impl Root for Value {}

fn main() {
    let value = Value(53);
    let object: &dyn Root<Item = u16> = &value;
    assert_eq!(object.value(), 53);
}
