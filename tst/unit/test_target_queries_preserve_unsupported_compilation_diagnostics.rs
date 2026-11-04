//@ reject-flags: --target=arm-unknown-linux-gnu || --target=armv7-unknown-linux-gnueabihf => only 64-bit little-endian targets are supported || E0463
//@ reject-flags: --target=powerpc64-unknown-linux-gnu => only 64-bit little-endian targets are supported || E0463
fn main() {
    let bytes = [3u8, 5, 7, 11];
    assert_eq!(bytes.iter().copied().map(u32::from).sum::<u32>(), 26);
}
