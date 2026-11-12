//@ aux-build: coverage_root_import_control.rs

extern crate coverage_root_import_control;

#[macro_export]
macro_rules! own_value {
    () => { 551u16 };
}

macro_rules! import_local_value {
    () => {{
        use crate as local_root;
        use local_root::own_value as selected;
        selected!()
    }};
}

fn main() {
    assert_eq!(import_local_value!(), 551);
    assert_eq!(coverage_root_import_control::import_both_paths!(), (557, 557));
}
