//@ crate-type: lib
//@ compile-flags: --emit=metadata
#![crate_name = "\0w1358"]

mod nested {
    pub(crate) fn visible() {}
}
