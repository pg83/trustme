include!("aux/drop_state_token.rs");
fn exercise(mode: u8, log: &Cell<u64>) {
    let values: [[Token<'_>; 2]; 32] = std::array::from_fn(|row| {
        [token(log, row as u32 * 2), token(log, row as u32 * 2 + 1)]
    });
    match mode {
        0 => { let [[first, _], ..] = values; drop(first); }
        1 => { let [.., [_, last]] = values; drop(last); }
        2 => drop(values),
        _ => {}
    }
    assert_eq!(log.get(), match mode { 0 => 1, 1 => 1 << 63, 2 => u64::MAX, _ => 0 });
}
fn main() {
    for mode in 0..4 {
        let log = Cell::new(0);
        exercise(std::hint::black_box(mode), &log);
        assert_eq!(log.get(), u64::MAX);
    }
}
