//@ edition: 2021
//@ proc-macro-aux-build: cov99_w297_owned_callback_tokens.rs
//@ aux-build: cov99_w297_owned_callback_table.rs
//@ compile-flags: --emit=metadata -Cpanic=abort
//@ compile-fail: Couldn't find the field missing_w297 in
//@ env: TRUSTME_DUMP_PROCMACRO=$OUT-w297

cov99_w297_owned_callback_tokens::inspect_drop!({
    callback.missing_w297;
});

fn main() {}
