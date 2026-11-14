//@ compile-fail: cov99_w801_named_macro_normalised
//@ compile-flags: --emit=metadata -Cpanic=abort

macro_rules! panic {
    ($message:tt) => {{
        macro_rules! held {
            () => { $message };
        }
        compile_error!("cov99_w801_named_macro_normalised");
    }};
}

fn main() {
    assert!(true, macro_rules! named { () => {} });
}
