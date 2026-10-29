#![feature(more_qualified_paths)]
enum Message<T> { Record { value: T }, Empty }
type Count = Message<u16>;
fn main() {
    let first = <Message<u8>>::Record { value: 7 };
    let second = <Count>::Record { value: 11 };
    match first { Message::Record { value } => assert_eq!(value, 7), _ => panic!() }
    match second { Count::Record { value } => assert_eq!(value, 11), _ => panic!() }
}
