enum Signal<T> { Idle, Ready, Value(T) }
type Alias<T> = Signal<T>;
type Nested<T> = Alias<T>;
fn code(value: Nested<u16>) -> u16 {
    match value {
        Nested::Idle => 3,
        Alias::Ready => 5,
        Signal::Value(value) => value + 7,
    }
}
struct Empty;
fn empty(Empty: Empty) -> u8 { 11 }
fn main() {
    assert_eq!(code(Alias::Idle), 3);
    assert_eq!(code(Nested::Ready), 5);
    assert_eq!(code(Alias::Value(13)), 20);
    assert_eq!(empty(Empty), 11);
}
