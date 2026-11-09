pub struct Unit;
pub struct Tuple(pub u8);

pub enum Choice {
    Unit,
    Tuple(u8),
    Named { value: u8 },
}
