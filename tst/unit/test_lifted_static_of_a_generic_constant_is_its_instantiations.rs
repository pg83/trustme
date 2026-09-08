//@ run-pass
/* owo-colors 4.3 `colors/custom.rs`: `impl<const R: u8, ..> Color for CustomColor<R, G, B> { const
   ANSI_FG: &'static str = bytes_to_str(&Self::ANSI_FG_U8); }` - the borrow of the inner constant
   is a static the evaluator lifts while evaluating `ANSI_FG` for an instantiation.  Lifted after
   the expander had moved the module's inline statics into its items, it was not found by the
   value lookup ("Could not find value name ..const0x..#0"); and named by the item and a
   per-evaluation index alone, every instantiation got the same static - `CustomColor<1, 2, 3>`
   read "7;8;9". */
const fn rgb_to_ansi(r: u8, g: u8, b: u8) -> [u8; 5] {
    [b'0' + r % 10, b';', b'0' + g % 10, b';', b'0' + b % 10]
}

const fn bytes_to_str(bytes: &'static [u8]) -> &'static str {
    match core::str::from_utf8(bytes) {
        Ok(s) => s,
        Err(_) => panic!("not utf8"),
    }
}

pub struct CustomColor<const R: u8, const G: u8, const B: u8>;

impl<const R: u8, const G: u8, const B: u8> CustomColor<R, G, B> {
    const ANSI_FG_U8: [u8; 5] = rgb_to_ansi(R, G, B);
}

pub trait Color {
    const ANSI_FG: &'static str;
}

impl<const R: u8, const G: u8, const B: u8> Color for CustomColor<R, G, B> {
    const ANSI_FG: &'static str = bytes_to_str(&Self::ANSI_FG_U8);
}

fn main() {
    assert_eq!(<CustomColor<1, 2, 3> as Color>::ANSI_FG, "1;2;3");
    assert_eq!(<CustomColor<7, 8, 9> as Color>::ANSI_FG, "7;8;9");
}
