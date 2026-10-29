use delegated_array_storage::Packet;
pub trait Windows {
    fn rotate(&self, input: Packet) -> Packet;
    fn prefix<const N: usize>(&self, input: [u8; N]) -> [u8; N];
}
pub struct Source;
impl Windows for Source {
    fn rotate(&self, input: [u16; 3]) -> [u16; 3] { [input[1], input[2], input[0]] }
    fn prefix<const N: usize>(&self, mut input: [u8; N]) -> [u8; N] {
        if N != 0 { input[0] += 1; }
        input
    }
}
