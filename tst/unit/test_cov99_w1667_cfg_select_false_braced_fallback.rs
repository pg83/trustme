#![feature(rustc_attrs, cfg_select)]

#[rustc_builtin_macro]
macro_rules! cfg_select {
    ($($tokens:tt)*) => {};
}

fn main() {
    let selected: u8 = cfg_select! {
        any() => { 16 }
        _ => { 67 }
    };
    assert_eq!(selected, 67);
}
