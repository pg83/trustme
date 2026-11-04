//@ compile-variant: -O
type Identity<T> = T;
struct Record(u16);
impl Record {
    fn owned(self: Identity<Identity<Self>>) -> u16 { self.0 }
    fn shared(self: Identity<Identity<&Self>>) -> u16 { self.0 + 1 }
}
fn main() {
    let value = Record(149);
    assert_eq!(value.shared(), 150);
    assert_eq!(Record::owned(value), 149);
}
