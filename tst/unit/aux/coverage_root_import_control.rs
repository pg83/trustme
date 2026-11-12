#![crate_type = "rlib"]

#[macro_export]
macro_rules! imported_value {
    () => { 557u16 };
}

#[macro_export]
macro_rules! import_both_paths {
    () => {{
        use $crate as external_root;
        use external_root::imported_value as selected;
        use $crate::imported_value as direct;
        (selected!(), direct!())
    }};
}
