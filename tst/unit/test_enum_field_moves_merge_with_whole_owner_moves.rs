include!("aux/drop_state_token.rs");
enum Parcel<'a> { Pair(Token<'a>, Token<'a>), Single(Token<'a>) }
fn exercise(mode: u8, pair: bool, log: &Cell<u64>) {
    let value = if pair { Parcel::Pair(token(log, 0), token(log, 1)) }
                else { Parcel::Single(token(log, 2)) };
    match mode {
        0 => {}
        1 => drop(value),
        _ => match value {
            Parcel::Pair(first, _) => drop(first),
            Parcel::Single(item) => drop(item),
        }
    }
    let expected = match mode { 0 => 0, 1 => if pair { 3 } else { 4 }, _ => if pair { 1 } else { 4 } };
    assert_eq!(log.get(), expected);
}
fn main() {
    for mode in 0..3 {
        for pair in [false, true] {
            let log = Cell::new(0);
            exercise(std::hint::black_box(mode), std::hint::black_box(pair), &log);
            assert_eq!(log.get(), if pair { 3 } else { 4 });
        }
    }
}
