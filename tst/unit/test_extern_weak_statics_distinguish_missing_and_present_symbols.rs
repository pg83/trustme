//@ aux-build: weak_function_defaults.rs
#![feature(linkage)]
extern "C" {
    #[linkage = "extern_weak"]
    static trustme_cov_absent_weak: *const u32;
    #[linkage = "extern_weak"]
    #[link_name = "trustme_cov_present_weak"]
    static PRESENT: *const u32;
}
fn main() {
    unsafe {
        assert!(trustme_cov_absent_weak.is_null());
        assert_eq!(PRESENT, weak_function_defaults::present_address());
        assert_eq!(*PRESENT, 37);
    }
}
