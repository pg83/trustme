enum Card { Empty, Pair(u16, u16), Named { number: u16, tail: u16 } }
enum Flag { Off, On }

fn select(value: Card) -> u16 {
    match value {
        Card::Empty => 3,
        Card::Pair(5, second) => second,
        Card::Pair { .. } => 7,
        Card::Named { number: 11, tail } => tail,
        Card::Named { .. } => 13,
    }
}

fn wildcard(value: Card) -> u16 {
    match value { Card::Empty {} => 17, _ => 19 }
}

fn main() {
    assert_eq!(select(Card::Empty), 3);
    assert_eq!(select(Card::Pair(5, 23)), 23);
    assert_eq!(select(Card::Pair(29, 31)), 7);
    assert_eq!(select(Card::Named { number: 11, tail: 37 }), 37);
    assert_eq!(select(Card::Named { number: 41, tail: 43 }), 13);
    assert_eq!(wildcard(Card::Empty), 17);
    assert_eq!(wildcard(Card::Pair(47, 53)), 19);
    assert_eq!(match Flag::On { Flag::Off {} => 59, Flag::On {} => 61 }, 61);
    assert_eq!(match Flag::Off { Flag::Off {} => 67, _ => 71 }, 67);
}
