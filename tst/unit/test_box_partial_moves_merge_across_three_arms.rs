include!("aux/drop_state_token.rs");
fn consume_first(mode: u8, log: &Cell<u64>) {
    let value = Box::new((token(log, 0), token(log, 1)));
    match mode {
        0 => drop(*value),
        1 => drop(value),
        _ => drop(value.0),
    }
    assert_eq!(log.get(), if mode < 2 { 3 } else { 1 });
}
fn keep_first(mode: u8, log: &Cell<u64>) {
    let value = Box::new((token(log, 0), token(log, 1)));
    match mode {
        0 => {}
        1 => drop(value),
        _ => drop(value.1),
    }
    assert_eq!(log.get(), [0, 3, 2][mode as usize]);
}
fn main() {
    for mode in 0..3 {
        let log = Cell::new(0);
        consume_first(std::hint::black_box(mode), &log);
        assert_eq!(log.get(), 3);
        log.set(0);
        keep_first(std::hint::black_box(mode), &log);
        assert_eq!(log.get(), 3);
    }
}
