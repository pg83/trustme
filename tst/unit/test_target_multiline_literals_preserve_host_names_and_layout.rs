//@ input: support/targets/literal_trimmed_host.toml
//@ input: support/targets/quoted_crlf_host.toml
//@ compile-variant: --target=$DIR/support/targets/literal_trimmed_host.toml --cfg quoted_target
//@ compile-variant: --target=$DIR/support/targets/quoted_crlf_host.toml --cfg quoted_target
#[repr(C)]
struct Pair { tag: u16, value: u64 }
fn main() {
    assert_eq!(std::mem::size_of::<Pair>(), 16);
    assert_eq!(std::mem::align_of::<Pair>(), 8);
    assert_eq!(u64::from_le_bytes([7, 1, 0, 0, 0, 0, 0, 0]), 263);
    #[cfg(quoted_target)]
    assert!(cfg!(all(target_family = "unix", target_os = "linux", target_env = "musl", target_arch = "x86_64")));
}
