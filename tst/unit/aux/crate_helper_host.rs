extern crate proc_macro_item_passthrough;

pub use proc_macro_item_passthrough::CallCrateHelper;

pub fn crate_helper() -> u8 {
    9
}

#[macro_export]
macro_rules! helper_via_derive {
    () => {{
        #[derive($crate::CallCrateHelper)]
        #[allow(dead_code)]
        enum Hack {}
        call_crate_helper!()
    }};
}
