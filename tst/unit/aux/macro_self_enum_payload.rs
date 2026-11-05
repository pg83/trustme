pub enum Payload {
    Named { value: u16 },
    Pair(u16),
    Empty,
}
