//@ aux-build: weak_function_defaults.rs
#![feature(linkage)]
#[export_name = "trustme_cov_selected_hook"]
pub extern "C" fn replacement() -> u32 { 29 }
#[linkage = "external"]
#[inline(never)]
fn ordinary() -> u32 { 31 }
fn main() {
    assert_eq!(weak_function_defaults::trustme_cov_default_hook(), 17);
    assert_eq!(weak_function_defaults::trustme_cov_selected_hook(), 29);
    assert_eq!(ordinary(), 31);
}
