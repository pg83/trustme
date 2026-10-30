struct Payload<const N: usize>([u8; N]);
struct Container<T = Payload<2>>(T);
impl Container {
    fn marker() -> u8 { 7 }
}
fn main() {
    let values = Container(Payload([11u8, 13]));
    assert_eq!(values.0.0[1], 13);
    assert_eq!(Container::marker(), 7);
}
