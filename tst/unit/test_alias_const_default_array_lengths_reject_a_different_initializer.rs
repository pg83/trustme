//@ compile-fail: Type mismatch
const fn width() -> usize { 2 + 3 }
type Buffer<const N: usize = { width() }> = [u16; N];

fn main() {
    let _: Buffer = [3u16; 4];
}
