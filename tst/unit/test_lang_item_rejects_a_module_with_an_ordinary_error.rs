//@ compile-fail: error:0: language item attribute is not supported on this item kind
#![feature(lang_items)]

#[lang = "sized"]
mod invalid {
    pub const VALUE: u16 = 919;
}

fn main() {
    let _ = invalid::VALUE;
}
