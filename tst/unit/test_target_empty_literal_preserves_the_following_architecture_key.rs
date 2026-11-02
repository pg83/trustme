//@ input: support/targets/empty_literal_host.toml
//@ input: support/targets/empty_literal_at_eof.toml
//@ input: support/targets/empty_basic_at_eof.toml
//@ compile-variant: --target=$DIR/support/targets/empty_literal_host.toml --cfg quoted_target
//@ compile-variant: --target=$DIR/support/targets/empty_literal_at_eof.toml --cfg quoted_target
//@ compile-variant: --target=$DIR/support/targets/empty_basic_at_eof.toml --cfg quoted_target
fn main() {
    let values = [13usize, 17, 19];
    assert_eq!(values.iter().copied().sum::<usize>(), 49);
    assert_eq!(std::mem::size_of::<usize>(), 8);
    #[cfg(quoted_target)]
    assert!(cfg!(all(target_env = "", target_arch = "x86_64")));
}
