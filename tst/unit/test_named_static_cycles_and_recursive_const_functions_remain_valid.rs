//@ compile-variant: -O
struct Node { value: u16, next: &'static Node }
static FIRST: Node = Node { value: 17, next: &SECOND };
static SECOND: Node = Node { value: 23, next: &FIRST };
const fn sum(value: u16) -> u16 {
    if value == 0 { 0 } else { value + sum(value - 1) }
}
const TOTAL: u16 = sum(4);
fn main() {
    assert_eq!(FIRST.next.value, 23);
    assert_eq!(SECOND.next.value, 17);
    assert!(std::ptr::eq(FIRST.next.next, &FIRST));
    assert_eq!(TOTAL, 10);
}
