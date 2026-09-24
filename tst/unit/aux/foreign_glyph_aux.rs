#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Glyph(pub u32);

impl Glyph {
    pub fn to_u32(self) -> u32 {
        self.0
    }
}
