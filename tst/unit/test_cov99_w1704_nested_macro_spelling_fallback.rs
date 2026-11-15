macro_rules! define_identity {
    ($dollar:tt, $capture:ident) => {
        macro_rules! identity {
            ($dollar $capture:expr) => { $dollar value };
        }
    };
}

define_identity!($, value);

fn main() {
    assert_eq!(identity!(37), 37);
}
