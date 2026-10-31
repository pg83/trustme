#[derive(PartialEq, Eq)]
struct Key { address: *const u8, number: u32, flag: bool }
const KEY: Key = Key { address: 37usize as *const u8, number: 41, flag: false };
fn classify(value: Key, allow: bool) -> u8 {
    match value { KEY if allow => 7, Key { address: _, number: 41, flag: false } => 11, _ => 13 }
}
fn main() {
    assert_eq!(classify(KEY, true), 7);
    assert_eq!(classify(KEY, false), 11);
    assert_eq!(classify(Key { address: 38usize as *const u8, number: 41, flag: false }, true), 11);
    assert_eq!(classify(Key { address: 37usize as *const u8, number: 42, flag: false }, true), 13);
}
