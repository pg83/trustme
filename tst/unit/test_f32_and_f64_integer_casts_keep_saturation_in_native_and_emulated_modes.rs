//@ compile-variant: --target=$DIR/emulated_i128_target.toml -Zmir-opt-level=0
//@ input: emulated_i128_target.toml
fn main() {
    macro_rules! check {
        ($float:ty, $integer:ty) => {{
            let nan = std::hint::black_box(<$float>::NAN);
            let positive = std::hint::black_box(<$float>::INFINITY);
            let negative = std::hint::black_box(<$float>::NEG_INFINITY);
            let finite = std::hint::black_box(19.75 as $float);
            assert_eq!(nan as $integer, 0);
            assert_eq!(positive as $integer, <$integer>::MAX);
            assert_eq!(negative as $integer, <$integer>::MIN);
            assert_eq!(finite as $integer, 19);
        }};
    }
    macro_rules! widths {
        ($float:ty) => {{
            check!($float, i8); check!($float, u8); check!($float, i16); check!($float, u16);
            check!($float, i32); check!($float, u32); check!($float, i64); check!($float, u64);
            check!($float, isize); check!($float, usize); check!($float, i128); check!($float, u128);
        }};
    }
    widths!(f32);
    widths!(f64);
}
