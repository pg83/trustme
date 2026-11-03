pub mod payload {
    pub struct Number(pub u32);
    pub enum Choice { Value(u32) }
    pub fn build(value: u32) -> Number { Number(value + 701) }
}
pub use payload as first;
pub mod nested { pub use super::first as second; }
pub use nested as doorway;
