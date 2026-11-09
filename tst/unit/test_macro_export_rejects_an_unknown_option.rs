//@ compile-fail: Unknown option for #[macro_export]

#[macro_export(unsupported)]
macro_rules! value {
    () => { 163u8 };
}

fn main() {}
