#![feature(linkage)]

#[no_mangle]
pub static trustme_linkage_present_value: u16 = 283;

#[linkage = "external"]
static ORDINARY: u16 = 287;

#[linkage = "weak"]
#[no_mangle]
pub static trustme_linkage_weak_value: u16 = 293;

#[linkage = "linkonce_odr"]
static ALIASED: u16 = 307;

#[linkage = "weak"]
#[inline(never)]
fn weak_function() -> u16 { 311 }

#[linkage = "internal"]
#[inline(never)]
fn local_function() -> u16 { 313 }

extern "C" {
    #[linkage = "extern_weak"]
    static trustme_linkage_missing_value: *const u16;

    #[linkage = "extern_weak"]
    #[link_name = "trustme_linkage_present_value"]
    static PRESENT: *const u16;
}

fn main() {
    unsafe {
        assert!(trustme_linkage_missing_value.is_null());
        assert_eq!(PRESENT, &trustme_linkage_present_value as *const u16);
        assert_eq!(core::ptr::read_volatile(PRESENT), 283);
        assert_eq!(core::ptr::read_volatile(&ORDINARY), 287);
        assert_eq!(core::ptr::read_volatile(&trustme_linkage_weak_value), 293);
        assert_eq!(core::ptr::read_volatile(&ALIASED), 307);
    }
    assert_eq!(weak_function() + local_function(), 624);
}
