#![feature(linkage)]
#[linkage = "weak"]
#[no_mangle]
#[inline(never)]
pub extern "C" fn trustme_cov_default_hook() -> u32 { 17 }
#[linkage = "weak"]
#[no_mangle]
#[inline(never)]
pub extern "C" fn trustme_cov_selected_hook() -> u32 { 19 }
#[no_mangle]
pub static trustme_cov_present_weak: u32 = 37;
#[inline(never)]
pub fn present_address() -> *const u32 { &trustme_cov_present_weak }
