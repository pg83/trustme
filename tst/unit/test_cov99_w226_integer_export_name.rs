//@ edition: 2021
//@ compile-fail: expected TOK_STRING
//@ compile-flags: --emit=metadata -Cpanic=abort

#[export_name = 7]
fn main() {}
