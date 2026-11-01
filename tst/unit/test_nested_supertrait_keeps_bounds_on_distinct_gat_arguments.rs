trait Source { type Cell<T>; }
trait Left: Source<Cell<u8>: Copy> {}
trait Both: Left<Cell<String>: Eq> {}
struct Value;
impl Source for Value { type Cell<T> = T; }
impl Left for Value {}
impl Both for Value {}

fn combine<I: Both>(byte: I::Cell<u8>, text: I::Cell<String>) -> (I::Cell<u8>, I::Cell<u8>, bool) {
    let copy = byte;
    (byte, copy, text == text)
}

fn main() {
    assert_eq!(combine::<Value>(37, String::from("nested")), (37, 37, true));
}
