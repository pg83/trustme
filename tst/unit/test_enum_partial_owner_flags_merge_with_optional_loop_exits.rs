include!("aux/drop_state_token.rs");
enum Parcel<'a> { Pair(Token<'a>, Token<'a>) }
fn exercise(first: bool, owner: bool, log: &Cell<u64>) {
    let value = Parcel::Pair(token(log, 0), token(log, 1));
    loop {
        if first { if owner { drop(value); } break; }
        loop {
            if owner { drop(value); break; }
            let Parcel::Pair(first, _) = value;
            drop(first);
            break;
        }
        break;
    }
    assert_eq!(log.get(), if owner { 3 } else if first { 0 } else { 1 });
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
