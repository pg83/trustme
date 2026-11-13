macro_rules! classify {
    ($dollar:tt $captured:expr) => {{
        macro_rules! choose {
            ($captured) => { 19u32 };
            ($dollar other:expr) => { 73u32 };
        }
        choose!($captured)
    }};
}

fn main() {
    assert_eq!(classify!($ 6u32), 73);
}
