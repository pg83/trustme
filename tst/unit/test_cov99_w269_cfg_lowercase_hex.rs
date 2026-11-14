//@ compile-flags: --cfg 'w269_hex="\x6a"'

#[cfg(not(w269_hex = "j"))]
compile_error!("CLI lowercase hexadecimal escape did not decode to j");

fn main() {}
