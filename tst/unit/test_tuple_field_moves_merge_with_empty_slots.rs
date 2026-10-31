include!("aux/drop_state_token.rs");
fn exercise(mode: u8, log: &Cell<u64>) {
    let value;
    match mode {
        0 => {}
        1 => { value = (token(log, 0), token(log, 1)); drop(value.0); }
        2 => { value = (token(log, 0), token(log, 1)); drop(value.1); }
        _ => { value = (token(log, 0), token(log, 1)); }
    }
    assert_eq!(log.get(), [0, 1, 2, 0][mode as usize]);
}
fn main() {
    for mode in 0..4 {
        let log = Cell::new(0);
        exercise(std::hint::black_box(mode), &log);
        assert_eq!(log.get(), if mode == 0 { 0 } else { 3 });
    }
}
