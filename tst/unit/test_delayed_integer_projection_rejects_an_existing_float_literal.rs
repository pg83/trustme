//@ compile-fail: error:

trait Output {
    type Value;
    fn value(self) -> Self::Value;
}
struct Word;
impl Output for Word {
    type Value = u16;
    fn value(self) -> u16 { 353 }
}
fn produce<T: Output>(source: T) -> T::Value {
    source.value()
}
fn main() {
    let mut result = 3.25;
    result = produce(Word);
    let _ = result;
}
