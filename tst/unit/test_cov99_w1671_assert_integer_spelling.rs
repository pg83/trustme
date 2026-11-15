//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort

macro_rules! panic {
    ($format:expr, $number:expr) => {{
        macro_rules! number_text {
            () => { stringify!($number) };
        }
        let _ = number_text!();
    }};
}

fn main() {
    assert!(true, "{}", 7u32);
}
