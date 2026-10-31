include!("aux/drop_state_token.rs");
fn exercise(branch: bool, log: &Cell<u64>) {
    let value = Box::new((token(log, 0), token(log, 1)));
    loop {
        if branch { drop(value.0); break; }
        drop(value.1);
        break;
    }
    assert_eq!(log.get(), if branch { 1 } else { 2 });
}
fn main() {
    for branch in [false, true] {
        let log = Cell::new(0);
        exercise(std::hint::black_box(branch), &log);
        assert_eq!(log.get(), 3);
    }
}
