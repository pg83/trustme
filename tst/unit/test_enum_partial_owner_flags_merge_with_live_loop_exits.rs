include!("aux/drop_state_token.rs");
enum Parcel<'a> { Pair(Token<'a>, Token<'a>) }
fn exercise(keep: bool, owner: bool, log: &Cell<u64>) {
    let value = Parcel::Pair(token(log, 0), token(log, 1));
    loop {
        if keep { break; }
        loop {
            if owner { drop(value); break; }
            let Parcel::Pair(first, _) = value;
            drop(first);
            break;
        }
        break;
    }
    assert_eq!(log.get(), if keep { 0 } else if owner { 3 } else { 1 });
}
fn main() {
    for keep in [false, true] {
        for owner in [false, true] {
            let log = Cell::new(0);
            exercise(std::hint::black_box(keep), std::hint::black_box(owner), &log);
            assert_eq!(log.get(), 3);
        }
    }
}
