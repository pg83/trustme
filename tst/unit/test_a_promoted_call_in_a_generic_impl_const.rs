// owo-colors 3.5: `impl<const R: u8, ..> Color for CustomColor<R, G, B>`
// with `const ANSI_FG: &'static str = unsafe { transmute(&rgb_to_ansi(R, G,
// B, true) as &[u8]) }`. The borrowed call becomes a promoted item generic
// over the impl's parameters, and its body is checked and evaluated with
// those parameters as its own.
const fn bytes(n: u8, bold: bool) -> [u8; 3] {
    [n, n + 1, if bold { 1 } else { 0 }]
}

trait Color {
    const SEQ: &'static [u8];
    const TEXT: &'static str;
}

struct Custom<const R: u8>;

impl<const R: u8> Color for Custom<R> {
    const SEQ: &'static [u8] = &bytes(R, true) as &[u8];
    const TEXT: &'static str = unsafe { core::mem::transmute(&bytes(R + 60, false) as &[u8]) };
}

fn main() {
    assert_eq!(<Custom<5> as Color>::SEQ, &[5, 6, 1]);
    assert_eq!(<Custom<5> as Color>::TEXT, "AB\0");
}
