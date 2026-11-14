//@ compile-only
//@ compile-flags: --emit=metadata --cfg 'cov99_w274="\x4A"'

#[cfg(not(cov99_w274 = "J"))]
compile_error!("uppercase hexadecimal cfg escape must decode to J");

fn main() {}
