//@ proc-macro-aux-build: item_passthrough_attribute.rs
// An attribute macro may annotate an `extern` block. rustc hands it the
// block's tokens: `unsafe extern "C" { .. }` or `extern "C" { .. }`, each
// foreign item as written, with no `unsafe` or `extern` of its own, a
// foreign `type Name;` among them. wasm-bindgen's
// `#[wasm_bindgen] extern "C" { type JsValue; .. }` blocks are such.
#![feature(extern_types)]
use item_passthrough_attribute::{foreign_types_end_at_their_name, passthrough};

#[no_mangle]
pub extern "C" fn trustme_triple(x: u32) -> u32 {
    x * 3
}

#[no_mangle]
pub static trustme_base: u32 = 4;

#[foreign_types_end_at_their_name]
extern "C" {
    #[link_name = "trustme_triple"]
    fn triple(x: u32) -> u32;
    #[link_name = "trustme_base"]
    static BASE: u32;
    type Opaque;
}

fn opaque_base() -> &'static Opaque {
    unsafe { &*(&raw const BASE as *const Opaque) }
}

#[passthrough]
unsafe extern "C" {
    #[link_name = "trustme_triple"]
    pub fn triple_again(x: u32) -> u32;
}

fn main() {
    assert_eq!(unsafe { triple(BASE) }, 12);
    assert_eq!(unsafe { triple_again(2) }, 6);
    assert_eq!(unsafe { *(opaque_base() as *const Opaque as *const u32) }, 4);
}
