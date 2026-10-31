include!("aux/drop_state_token.rs");
fn exercise(mode: u8, log: &Cell<u64>) {
    let values: [Token<'_>; 32];
    match mode {
        0 => {}
        1 => {
            values = std::array::from_fn(|i| token(log, i as u32));
            let [first, ..] = values;
            drop(first);
        }
        _ => {
            values = std::array::from_fn(|i| token(log, i as u32));
            let [.., last] = values;
            drop(last);
        }
    }
    assert_eq!(log.get(), match mode { 0 => 0, 1 => 1, _ => 1 << 31 });
}
fn main() {
    for mode in 0..3 {
        let log = Cell::new(0);
        exercise(std::hint::black_box(mode), &log);
        assert_eq!(log.get(), if mode == 0 { 0 } else { u32::MAX as u64 });
    }
}
