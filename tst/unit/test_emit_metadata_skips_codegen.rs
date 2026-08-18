// `--emit=metadata` asks for the crate to be analysed but not built: nothing
// is codegenned and nothing is linked, so a crate that only declares an
// external symbol is still valid.
//@ crate-type: lib
//@ compile-flags: --emit=metadata

extern "C" {
    fn a_symbol_no_one_defines();
}

pub fn call() {
    unsafe { a_symbol_no_one_defines() }
}
