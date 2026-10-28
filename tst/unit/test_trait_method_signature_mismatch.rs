//@ compile-fail: doesn't match trait

trait Convert {
    fn convert(&self, input: u16) -> u8;
}

struct Converter;
impl Convert for Converter {
    fn convert(&self, input: u32) -> u16 { input as u16 }
}

fn main() {}
