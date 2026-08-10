macro_rules! check_max_bits {
    ($ty:ty, $expected:expr) => {
        let bit_indexes = 0..<$ty>::MAX.count_ones();
        assert_eq!(bit_indexes.count(), $expected);
    };
}

fn main() {
    check_max_bits!(u8, 8);
}
