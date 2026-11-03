extern crate std;
extern crate core as primitives;
extern crate self as owner;
pub const TAG: u16 = 379;
macro_rules! capture {
    ($declaration:item) => { const CAPTURED: &str = stringify!($declaration); };
}
capture!(extern crate self;);
fn main() {
    assert_eq!(std::mem::size_of::<u16>(), 2);
    assert_eq!(primitives::mem::size_of::<u32>(), 4);
    assert_eq!(owner::TAG, 379);
    assert!(!CAPTURED.is_empty());
}
