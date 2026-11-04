//@ compile-variant: -O
type Identity<T = u16> = T;
type Handle<T = Record> = Identity<T>;
struct Record(u16);
impl Record {
    fn shared(self: &Handle) -> u16 { self.0 + 1 }
    fn owned(self: Handle) -> u16 { self.0 }
}
fn main() {
    let value = Record(179);
    assert_eq!(value.shared(), 180);
    assert_eq!(Record::owned(value), 179);
}
