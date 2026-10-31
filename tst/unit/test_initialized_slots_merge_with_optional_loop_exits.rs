include!("aux/drop_state_token.rs");
fn exercise(first: bool, second: bool, log: &Cell<u64>) {
    let value;
    loop {
        if first { value = token(log, 0); break; }
        if second { value = token(log, 0); }
        break;
    }
    assert_eq!(log.get(), 0);
}
fn main() {
    for first in [false, true] {
        for second in [false, true] {
            let log = Cell::new(0);
            exercise(std::hint::black_box(first), std::hint::black_box(second), &log);
            assert_eq!(log.get(), if first || second { 1 } else { 0 });
        }
    }
}
