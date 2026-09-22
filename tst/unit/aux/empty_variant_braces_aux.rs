#[derive(Debug, PartialEq)]
pub enum Set {
    SetNamesDefault {},
    Tuple(),
    Unit,
    Named { a: u8 },
}
