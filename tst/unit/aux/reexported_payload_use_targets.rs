pub enum Payload {
    Plain,
    Pair(u8, u16),
    Named { value: u16 },
}
