pub trait Signal {}

impl dyn Signal {
    pub fn answer() -> u8 {
        37
    }
}
