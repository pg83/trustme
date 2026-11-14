//@ compile-flags: -Zstop-after=parse
//@ crate-type: lib

pub fn legacy_catch() {
    let _ = do catch {};
}
