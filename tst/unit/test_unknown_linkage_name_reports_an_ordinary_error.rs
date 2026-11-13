//@ compile-fail: error:0: unsupported linkage name `trustme_missing_linkage`
#![feature(linkage)]

#[linkage = "trustme_missing_linkage"]
extern "C" fn linked_value() -> u16 {
    941
}

fn main() {
    let _ = linked_value();
}
