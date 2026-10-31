fn increment(value: u8) -> u8 { value + 1 }
fn opaque() -> impl Fn(u8) -> u8 { |value| value + 3 }
fn dynamic(value: &dyn Fn(u8) -> u8) -> u8 {
    match (true, value) { (true, selected) => selected(17), (false, _) => 0 }
}
fn main() {
    let closure = |value| value + 2;
    match (increment, 1u8) { (_, 0) => panic!(), (selected, _) => assert_eq!(selected(17), 18) }
    let function: fn(u8) -> u8 = increment;
    match (function, 2u8) { (_, 0) => panic!(), (selected, _) => assert_eq!(selected(23), 24) }
    match (closure, 3u8) { (_, 0) => panic!(), (selected, _) => assert_eq!(selected(37), 39) }
    assert_eq!(dynamic(&opaque()), 20);
    let value = 41u8;
    let pointer = &value as *const u8;
    assert_eq!(match (true, pointer) { (true, _) => value, _ => 0 }, 41);
}
