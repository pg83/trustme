// An ABI may reach the parser as a `$abi:literal` fragment rather than a bare
// string. Every place that reads one demanded the token itself.
//
// Same shape as the ui test parser/macro/extern-abi-from-mac-literal-frag.rs.
#![allow(clashing_extern_declarations)]

macro_rules! abi_from_lit_frag {
    ($abi:literal) => {
        unsafe extern $abi {
            fn _import();
        }

        extern $abi fn _export() {}

        type _PTR = extern $abi fn();
    };
}

abi_from_lit_frag!("C");

fn main() {
    let _ = _export as extern "C" fn();
}
