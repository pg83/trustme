//@ compile-variant: -O
#![feature(extern_types)]
#[export_name = "coverage_foreign_raw_object"]
static BYTE: u8 = 29;
extern "C" {
    type Opaque;
    #[link_name = "coverage_foreign_raw_object"]
    static FOREIGN: Opaque;
}
const VALUE: *const Opaque = unsafe { &FOREIGN as *const Opaque };
fn main() { assert_eq!(unsafe { *(VALUE as *const u8) }, 29); }
