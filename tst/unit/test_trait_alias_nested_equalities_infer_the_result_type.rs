#![feature(trait_alias)]
trait Source { type Stream; fn stream(self) -> Self::Stream; }
trait Items<Value> = Source<Stream: Iterator<Item = Value>>;
struct Numbers;
impl Source for Numbers {
    type Stream = std::vec::IntoIter<u16>;
    fn stream(self) -> Self::Stream { vec![3u16, 5].into_iter() }
}
trait Extract<Value> { fn extract(self) -> Value; }
impl<T: Items<Value>, Value> Extract<Value> for T {
    fn extract(self) -> Value { let mut stream = self.stream(); stream.next().unwrap() }
}
fn main() {
    let value = Numbers.extract();
    assert_eq!(std::mem::size_of_val(&value), 2);
    assert_eq!(value, 3);
}
