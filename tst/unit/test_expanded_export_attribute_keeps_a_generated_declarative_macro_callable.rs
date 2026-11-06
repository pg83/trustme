#![feature(decl_macro)]

macro_rules! declare_sum {
    ($dollar:tt) => {
        #[macro_export]
        pub macro generated_sum($dollar left:expr, $dollar right:expr) {
            $dollar left + $dollar right
        }
    };
}

declare_sum!($);

fn main() {
    assert_eq!(crate::generated_sum!(17u16, 29u16), 46);
}
