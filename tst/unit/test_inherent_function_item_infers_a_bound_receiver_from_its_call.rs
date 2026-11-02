trait Marker {}
impl Marker for u16 {}
struct Package<T>(T);
impl<T: Marker> Package<T> {
    fn build(value: T) -> Self { Package(value) }
}

fn main() {
    let build = Package::<_>::build;
    let package = build(71u16);
    assert_eq!(package.0, 71);
}
