include!("aux/drop_state_token.rs");
fn inner_first(branch: bool, log: &Cell<u64>) {
    let value = Box::new(token(log, 0));
    loop {
        if branch { drop(*value); break; }
        drop(value);
        break;
    }
    assert_eq!(log.get(), 1);
}
fn owner_first(branch: bool, log: &Cell<u64>) {
    let value = Box::new(token(log, 0));
    loop {
        if branch { drop(value); break; }
        drop(*value);
        break;
    }
    assert_eq!(log.get(), 1);
}
fn main() {
    for branch in [false, true] {
        let log = Cell::new(0);
        inner_first(std::hint::black_box(branch), &log);
        assert_eq!(log.get(), 1);
        log.set(0);
        owner_first(std::hint::black_box(branch), &log);
        assert_eq!(log.get(), 1);
    }
}
