//@ crate-type: lib
//@ compile-flags: --emit=metadata

pub fn deferred_let_tail() -> ! {
    let _ = (|| -> ! { loop {} })();
}
