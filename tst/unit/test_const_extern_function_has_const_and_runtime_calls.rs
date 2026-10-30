const extern "C" fn advance(value: u16) -> u16 { value + 3 }
const ANSWER: u16 = advance(11);
fn main() {
    assert_eq!(ANSWER, 14);
    let call: extern "C" fn(u16) -> u16 = advance;
    assert_eq!(call(std::hint::black_box(17)), 20);
}
