include!("aux/drop_state_token.rs");
macro_rules! array_case {
    ($name:ident, $length:expr) => {
        fn $name(mode: u8, log: &Cell<u64>) {
            let values: [Token<'_>; $length] = std::array::from_fn(|i| token(log, i as u32));
            match mode {
                0 => {}
                1 => { let [first, ..] = values; drop(first); }
                2 => { let [_, second, ..] = values; drop(second); }
                3 => { let [.., last] = values; drop(last); }
                _ => drop(values),
            }
            let expected = match mode {
                0 => 0,
                1 => 1,
                2 => 2,
                3 => 1 << ($length - 1),
                _ => (1u64 << $length) - 1,
            };
            assert_eq!(log.get(), expected);
        }
    };
}
array_case!(small, 31);
array_case!(threshold, 32);
array_case!(larger, 33);
fn main() {
    for mode in 0..5 {
        let log = Cell::new(0);
        small(std::hint::black_box(mode), &log);
        assert_eq!(log.get(), (1u64 << 31) - 1);
        log.set(0);
        threshold(std::hint::black_box(mode), &log);
        assert_eq!(log.get(), (1u64 << 32) - 1);
        log.set(0);
        larger(std::hint::black_box(mode), &log);
        assert_eq!(log.get(), (1u64 << 33) - 1);
    }
}
