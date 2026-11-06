#![feature(decl_macro)]

macro_rules! declare_add {
    ($dollar:tt) => {
        #[macro_export]
        pub macro generated_add($dollar value:expr) {
            $dollar value + 7u16
        }
    };
}

declare_add!($);

mod definitions {
    #[macro_export]
    macro_rules! wrapped_add {
        ($value:expr) => { $crate::generated_add!($value) };
    }
}

fn main() {
    assert_eq!(wrapped_add!(31u16), 38);
    assert_eq!(crate::generated_add!(19u16), 26);
}
