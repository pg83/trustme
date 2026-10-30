struct Payload<const N: usize>([u8; N]);
struct Container<T = Payload<{ 1 + 2 }>>(T);
impl Container {
    fn marker() -> u8 { 19 }
}
fn main() {
    let values = Container(Payload([5u8, 7, 11]));
    assert_eq!(values.0.0[2], 11);
    assert_eq!(Container::marker(), 19);
}
