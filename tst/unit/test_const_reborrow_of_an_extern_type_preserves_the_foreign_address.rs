//@ compile-variant: -O
#![feature(extern_types)]
#[export_name = "coverage_foreign_object"]
static BYTE: u8 = 23;
extern "C" {
    type Opaque;
    #[link_name = "coverage_foreign_object"]
    static FOREIGN: Opaque;
}
const VALUE: &Opaque = unsafe { &*(&FOREIGN as *const Opaque) };
fn main() {
    assert_eq!(unsafe { *(VALUE as *const Opaque as *const u8) }, 23);
}
