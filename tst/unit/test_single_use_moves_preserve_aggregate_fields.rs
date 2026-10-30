//@ compile-flags: -Zmir-opt-level=3
#[derive(Debug, PartialEq)]
struct Record { code: u16, payload: [u8; 3] }
#[derive(Debug, PartialEq)]
enum Message { Data(u16, [u8; 3]), Empty }
#[inline(never)]
fn array(value: [u8; 3]) -> [u8; 3] {
    let made = [value[0], value[1], value[2]];
    let next = made;
    next
}
#[inline(never)]
fn pair(value: (u16, [u8; 3])) -> (u16, [u8; 3]) {
    let made = (value.0, value.1);
    let next = made;
    next
}
#[inline(never)]
fn record(value: Record) -> Record {
    let made = Record { code: value.code, payload: value.payload };
    let next = made;
    next
}
#[inline(never)]
fn message(value: Message) -> Message {
    let made = match value {
        Message::Data(code, payload) => Message::Data(code, payload),
        Message::Empty => Message::Empty,
    };
    let next = made;
    next
}
#[inline(never)]
fn make_pair() -> (u16, u16) { std::hint::black_box((23, 29)) }
#[inline(never)]
fn combine(left: u16, right: &mut u16) -> u16 {
    *right += 1;
    left + *right
}
fn main() {
    let mut fields = make_pair();
    let left = fields.0;
    let mut other = 29;
    let right = &mut other;
    assert_eq!(combine(left, right), 53);
    assert_eq!(fields, (23, 29));
    assert_eq!(other, 30);
    assert_eq!(array([3, 5, 7]), [3, 5, 7]);
    assert_eq!(pair((11, [13, 17, 19])), (11, [13, 17, 19]));
    assert_eq!(record(Record { code: 23, payload: [29, 31, 37] }), Record { code: 23, payload: [29, 31, 37] });
    assert_eq!(message(Message::Data(41, [43, 47, 53])), Message::Data(41, [43, 47, 53]));
    assert_eq!(message(Message::Empty), Message::Empty);
}
