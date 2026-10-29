//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
struct Record { left: u8, right: u8 }
struct Pair(u8, u8);
macro_rules! relay {
    ($item:item) => { proc_macro_item_passthrough::echo! { $item } };
}
relay! {
    fn sum(Record { left, right }: Record) -> u8 { left + right }
}
relay! {
    fn first(Record { left, .. }: Record) -> u8 { left }
}
relay! {
    fn second(Pair { 1: value, .. }: Pair) -> u8 { value }
}
relay! {
    fn pair_sum(Pair { 0: left, 1: right }: Pair) -> u8 { left + right }
}
fn main() {
    assert_eq!(sum(Record { left: 5, right: 7 }), 12);
    assert_eq!(first(Record { left: 3, right: 9 }), 3);
    assert_eq!(second(Pair(11, 17)), 17);
    assert_eq!(pair_sum(Pair(13, 19)), 32);
}
