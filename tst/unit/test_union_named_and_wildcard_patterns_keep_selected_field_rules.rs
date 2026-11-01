union Choice { word: u16, pair: (u8, u8) }
fn word(value: Choice) -> u16 {
    unsafe { match value { Choice { word: 3..=5 } => 7, _ => 11 } }
}
fn pair(value: Choice) -> u16 {
    unsafe { match value { Choice { pair: (13, second) } => second as u16, _ => 17 } }
}
fn main() {
    assert_eq!(word(Choice { word: 3 }), 7);
    assert_eq!(word(Choice { word: 9 }), 11);
    assert_eq!(pair(Choice { pair: (13, 19) }), 19);
    assert_eq!(pair(Choice { pair: (23, 29) }), 17);
}
