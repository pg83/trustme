include!("aux/drop_state_token.rs");
fn exercise(keep: bool, owner: bool, log: &Cell<u64>) {
    let value = Box::new(token(log, 0));
    loop {
        if keep { break; }
        loop {
            if owner { drop(value); break; }
            drop(*value);
            break;
        }
        break;
    }
    assert_eq!(log.get(), if keep { 0 } else { 1 });
}
fn main() {
    for keep in [false, true] {
        for owner in [false, true] {
            let log = Cell::new(0);
            exercise(std::hint::black_box(keep), std::hint::black_box(owner), &log);
            assert_eq!(log.get(), 1);
        }
    }
}
