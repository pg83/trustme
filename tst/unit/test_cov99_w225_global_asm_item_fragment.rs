//@ compile-only
//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort

macro_rules! duplicate_item {
    ($item:item) => {
        mod first { $item }
        mod second { $item }
    };
}

duplicate_item! {
    mod inner {
        core::arch::global_asm!("");
    }
}
