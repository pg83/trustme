mod leaf;
mod inner;
pub fn total() -> u8 { leaf::VALUE + inner::VALUE }
