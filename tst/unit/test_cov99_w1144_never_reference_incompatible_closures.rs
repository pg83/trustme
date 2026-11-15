//@ crate-type: lib
//@ compile-flags: --emit=metadata
//@ compile-fail: Type mismatch between
#![feature(never_type)]

fn never_reference() -> &'static ! {
    loop {}
}

pub fn choose(flag: bool) {
    let _closure = if flag {
        || -> &'static _ { never_reference() }
    } else {
        |_: u8| -> &'static _ { never_reference() }
    };
}
