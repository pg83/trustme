include!("aux/drop_state_token.rs");
fn exercise(first: bool, inner: bool, log: &Cell<u64>) {
    let value = Box::new(token(log, 0));
    loop {
        if first {
            if inner { drop(value); } else { drop(*value); }
            break;
        }
        if inner { drop(value); }
        break;
    }
    assert_eq!(log.get(), if first || inner { 1 } else { 0 });
}
fn main() {
    for first in [false, true] {
        for inner in [false, true] {
            let log = Cell::new(0);
            exercise(std::hint::black_box(first), std::hint::black_box(inner), &log);
            assert_eq!(log.get(), 1);
        }
    }
}
