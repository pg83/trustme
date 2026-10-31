include!("aux/drop_state_token.rs");
fn owner_then_inner(outer: bool, inner: bool, log: &Cell<u64>) {
    let value = Box::new(token(log, 0));
    if outer {
        if inner { drop(value); } else { drop(*value); }
    }
    assert_eq!(log.get(), if outer { 1 } else { 0 });
}
fn inner_then_owner(outer: bool, inner: bool, log: &Cell<u64>) {
    let value = Box::new(token(log, 0));
    if outer {
        if inner { drop(*value); } else { drop(value); }
    }
    assert_eq!(log.get(), if outer { 1 } else { 0 });
}
fn main() {
    for outer in [false, true] {
        for inner in [false, true] {
            let log = Cell::new(0);
            owner_then_inner(std::hint::black_box(outer), std::hint::black_box(inner), &log);
            assert_eq!(log.get(), 1);
            log.set(0);
            inner_then_owner(std::hint::black_box(outer), std::hint::black_box(inner), &log);
            assert_eq!(log.get(), 1);
        }
    }
}
