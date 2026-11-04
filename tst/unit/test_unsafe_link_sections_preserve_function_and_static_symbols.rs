//@ compile-variant: -O

#[unsafe(no_mangle)]
#[unsafe(link_section = ".rodata.trustme_own_section_value")]
pub static TRUSTME_OWN_SECTION_VALUE: u32 = 29;

#[inline(never)]
#[unsafe(export_name = "trustme_own_section_add")]
#[unsafe(link_section = ".text.trustme_own_section_add")]
pub extern "C" fn add(value: u32) -> u32 {
    value + TRUSTME_OWN_SECTION_VALUE
}

extern "C" {
    #[link_name = "trustme_own_section_add"]
    fn imported_add(value: u32) -> u32;
    #[link_name = "TRUSTME_OWN_SECTION_VALUE"]
    static IMPORTED_VALUE: u32;
}

fn main() {
    assert_eq!(add(14), 43);
    assert_eq!(unsafe { imported_add(14) }, 43);
    assert_eq!(unsafe { IMPORTED_VALUE }, 29);
    assert_eq!(unsafe { &IMPORTED_VALUE as *const u32 }, &TRUSTME_OWN_SECTION_VALUE);
}
