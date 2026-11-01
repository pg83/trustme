trait Source { type Cell<T>; }
trait Left: Source<Cell<u8>: Copy> {}
trait Right: Source<Cell<String>: Eq> {}
trait Both: Left + Right {}
struct Value;
impl Source for Value { type Cell<T> = T; }
impl Left for Value {}
impl Right for Value {}
impl Both for Value {}

fn combine<I: Both>(byte: I::Cell<u8>, text: I::Cell<String>) -> (I::Cell<u8>, I::Cell<u8>, bool) {
    let copy = byte;
    (byte, copy, text == text)
}

fn main() {
    assert_eq!(combine::<Value>(21, String::from("ready")), (21, 21, true));
}
