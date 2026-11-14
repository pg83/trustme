//@ edition: 2021
//@ compile-flags: --emit=metadata -Cpanic=abort --cfg cov99_w276_value=unquoted
//@ compile-fail: invalid `--cfg` argument `cov99_w276_value=unquoted`: expected a string literal

fn main() {}
