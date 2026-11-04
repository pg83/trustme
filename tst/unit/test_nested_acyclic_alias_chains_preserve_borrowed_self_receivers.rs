//@ compile-variant: -O
type Inner<T> = T;
type Outer<T> = Inner<T>;
struct Record(u16);
impl Record {
    fn read(self: &Outer<Outer<Self>>) -> u16 { self.0 }
}
fn main() { assert_eq!(Record(163).read(), 163); }
