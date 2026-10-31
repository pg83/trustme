enum Event { Empty, Pair(u8, bool), Named { number: u8, flag: bool } }
fn classify(value: Event, allow: bool) -> u8 {
    match value {
        Event::Empty if allow => 7,
        Event::Pair(37, false) if allow => 11,
        Event::Pair { .. } => 13,
        Event::Named { number: 41, .. } if allow => 17,
        _ => 19,
    }
}
fn main() {
    assert_eq!(classify(Event::Empty, true), 7);
    assert_eq!(classify(Event::Pair(37, false), true), 11);
    assert_eq!(classify(Event::Pair(37, false), false), 13);
    assert_eq!(classify(Event::Named { number: 41, flag: true }, true), 17);
    assert_eq!(classify(Event::Named { number: 42, flag: false }, true), 19);
}
