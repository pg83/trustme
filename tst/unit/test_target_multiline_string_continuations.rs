//@ compile-flags: --target=$DIR/support/targets/multiline_continuations.toml
//@ input: support/targets/multiline_continuations.toml
#[cfg(not(all(target_family = "unix", target_os = "linux", target_env = "musl", target_arch = "x86_64")))]
compile_error!("continued target strings must preserve their joined values");
fn main() {
    assert_eq!(std::mem::size_of::<usize>(), 8);
    assert_eq!(u64::from_le_bytes([1, 2, 0, 0, 0, 0, 0, 0]), 513);
    let values = [3u16, 5, 8];
    assert_eq!(values.iter().copied().sum::<u16>(), 16);
}
