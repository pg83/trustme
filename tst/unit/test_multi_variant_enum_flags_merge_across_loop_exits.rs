include!("aux/drop_state_token.rs");
enum Parcel<'a> { Pair(Token<'a>, Token<'a>), Single(Token<'a>) }
fn parcel(log: &Cell<u64>, pair: bool) -> Parcel<'_> {
    if pair { Parcel::Pair(token(log, 0), token(log, 1)) }
    else { Parcel::Single(token(log, 2)) }
}
macro_rules! move_one {
    ($value:ident) => {
        match $value { Parcel::Pair(first, _) => drop(first), Parcel::Single(item) => drop(item) }
    };
}
fn live_first(first: bool, owner: bool, pair: bool, log: &Cell<u64>) {
    let value = parcel(log, pair);
    loop {
        if first { break; }
        loop {
            if owner { drop(value); break; }
            move_one!(value);
            break;
        }
        break;
    }
    let expected = if first { 0 } else if owner { if pair { 3 } else { 4 } } else if pair { 1 } else { 4 };
    assert_eq!(log.get(), expected);
}
fn optional_first(first: bool, owner: bool, pair: bool, log: &Cell<u64>) {
    let value = parcel(log, pair);
    loop {
        if first { if owner { drop(value); } break; }
        loop {
            if owner { drop(value); break; }
            move_one!(value);
            break;
        }
        break;
    }
    let expected = if owner { if pair { 3 } else { 4 } } else if first { 0 } else if pair { 1 } else { 4 };
    assert_eq!(log.get(), expected);
}
fn partial_first(first: bool, owner: bool, pair: bool, log: &Cell<u64>) {
    let value = parcel(log, pair);
    loop {
        if first { move_one!(value); break; }
        if owner { drop(value); }
        break;
    }
    let expected = if first { if pair { 1 } else { 4 } } else if owner { if pair { 3 } else { 4 } } else { 0 };
    assert_eq!(log.get(), expected);
}
fn main() {
    for first in [false, true] {
        for owner in [false, true] {
            for pair in [false, true] {
                let log = Cell::new(0);
                live_first(std::hint::black_box(first), std::hint::black_box(owner), std::hint::black_box(pair), &log);
                assert_eq!(log.get(), if pair { 3 } else { 4 });
                log.set(0);
                optional_first(std::hint::black_box(first), std::hint::black_box(owner), std::hint::black_box(pair), &log);
                assert_eq!(log.get(), if pair { 3 } else { 4 });
                log.set(0);
                partial_first(std::hint::black_box(first), std::hint::black_box(owner), std::hint::black_box(pair), &log);
                assert_eq!(log.get(), if pair { 3 } else { 4 });
            }
        }
    }
}
