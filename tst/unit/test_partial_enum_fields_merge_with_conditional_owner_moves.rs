include!("aux/drop_state_token.rs");
enum Parcel<'a> { Pair(Token<'a>, Token<'a>) }
fn exercise(first: bool, owner: bool, log: &Cell<u64>) {
    let value = Parcel::Pair(token(log, 0), token(log, 1));
    loop {
        if first { let Parcel::Pair(item, _) = value; drop(item); break; }
        if owner { drop(value); }
        break;
    }
    assert_eq!(log.get(), if first { 1 } else if owner { 3 } else { 0 });
}
fn main() {
    for first in [false, true] {
        for owner in [false, true] {
            let log = Cell::new(0);
            exercise(std::hint::black_box(first), std::hint::black_box(owner), &log);
            assert_eq!(log.get(), 3);
        }
    }
}
