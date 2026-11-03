fn forward(value: u32) -> u32 { value + 613 }
macro_rules! twice { ($value:expr) => { forward($value) + forward($value) }; }
fn main() {
    assert_eq!(forward(4), 617);
    assert_eq!(twice!(7), 1240);
}
