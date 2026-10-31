include!("aux/drop_state_token.rs");
enum Parcel<'a> { Pair(Token<'a>, Token<'a>), Single(Token<'a>) }
fn exercise(mode: u8, owner: bool, log: &Cell<u64>) {
    let value = Parcel::Pair(token(log, 0), token(log, 1));
    match mode {
        0 => {}
        1 => drop(value),
        _ => if owner { drop(value); } else {
            match value {
                Parcel::Pair(_, last) => drop(last),
                Parcel::Single(item) => drop(item),
            }
        },
    }
    let expected = match mode { 0 => 0, 1 => 3, _ => if owner { 3 } else { 2 } };
    assert_eq!(log.get(), expected);
}
fn main() {
    for mode in 0..3 {
        for owner in [false, true] {
            let log = Cell::new(0);
            exercise(std::hint::black_box(mode), std::hint::black_box(owner), &log);
            assert_eq!(log.get(), 3);
        }
    }
    let log = Cell::new(0);
    drop(Parcel::Single(token(&log, 2)));
    assert_eq!(log.get(), 4);
}
