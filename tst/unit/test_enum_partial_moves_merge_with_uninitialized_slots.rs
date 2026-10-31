include!("aux/drop_state_token.rs");
enum Parcel<'a> { Pair(Token<'a>, Token<'a>), Empty }
fn exercise(mode: u8, log: &Cell<u64>) {
    let value;
    match mode {
        0 => {}
        _ => {
            value = Parcel::Pair(token(log, 0), token(log, 1));
            match value {
                Parcel::Pair(first, _) => drop(first),
                Parcel::Empty => {}
            }
        }
    }
    assert_eq!(log.get(), if mode == 0 { 0 } else { 1 });
}
fn main() {
    for mode in 0..2 {
        let log = Cell::new(0);
        exercise(std::hint::black_box(mode), &log);
        assert_eq!(log.get(), if mode == 0 { 0 } else { 3 });
    }
    drop(Parcel::Empty);
}
