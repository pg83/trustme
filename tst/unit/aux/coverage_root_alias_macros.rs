#![crate_type = "rlib"]

#[macro_export]
macro_rules! external_value {
    () => { 547u16 };
}

#[macro_export]
macro_rules! through_external_root {
    () => {{
        use $crate as origin;
        origin::external_value!()
    }};
}
