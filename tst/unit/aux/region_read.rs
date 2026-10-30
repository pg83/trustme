pub trait Read<'a> { type Output; fn read(&self) -> Self::Output; }
pub struct Input<'a>(pub &'a u16);
impl<'a> Read<'a> for Input<'a> { type Output = &'a u16; fn read(&self) -> &'a u16 { self.0 } }
