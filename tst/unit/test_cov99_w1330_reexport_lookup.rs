//@ crate-type: lib
//@ compile-flags: -Zdump-ast -Zdump-hir

mod origin {
    pub fn value() -> u8 {
        37
    }
}

mod route {
    use crate::origin as module_alias;
    pub use module_alias::value as first;
}

mod relay {
    pub use crate::route::first as second;
}

pub use relay::second as exported;
