//@ proc-macro-aux-build: numeric_token_bridge.rs
//@ aux-build: exported_numeric_tokens.rs
#![feature(f128)]
use numeric_token_bridge::echo;
fn main() {
    assert_eq!(exported_numeric_tokens::exported_unsigned!(echo), (65535u16, 1u128 << 64));
    assert_eq!(exported_numeric_tokens::exported_signed!(echo), (-127i8, i128::MIN));
    assert_eq!(exported_numeric_tokens::exported_float!(echo).to_bits(), 1.000000000000000000000000000001f128.to_bits());
}
