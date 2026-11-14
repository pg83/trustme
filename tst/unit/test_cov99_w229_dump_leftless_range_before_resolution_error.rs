//@ compile-flags: -Zdump-ast
//@ compile-fail: Couldn't find variable name 'missing_cov99_w229'

fn main() {
    (..=1u8) += missing_cov99_w229;
}
