//@ aux-build: coverage_root_alias_macros.rs

extern crate coverage_root_alias_macros;

#[macro_export]
macro_rules! local_value {
    () => { 541u16 };
}

macro_rules! through_local_root {
    () => {{
        use crate as origin;
        origin::local_value!()
    }};
}

fn main() {
    assert_eq!(through_local_root!(), 541);
    assert_eq!(coverage_root_alias_macros::through_external_root!(), 547);
}
