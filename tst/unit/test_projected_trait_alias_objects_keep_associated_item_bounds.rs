#![feature(trait_alias)]

trait CopyWords = Iterator<Item: Copy>;
trait Shape {
    type View: ?Sized;
}
struct Spec;
impl Shape for Spec {
    type View = dyn CopyWords<Item = u16>;
}

fn view<I: Iterator<Item = u16> + 'static>(input: &mut I) -> &mut <Spec as Shape>::View {
    input
}

fn main() {
    let mut words = [1091u16, 1093].into_iter();
    let object = view(&mut words);
    assert_eq!(object.next(), Some(1091));
    assert_eq!(object.next(), Some(1093));
    assert!(object.next().is_none());
}
