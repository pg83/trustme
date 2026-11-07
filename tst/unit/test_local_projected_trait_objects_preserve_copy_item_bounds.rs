#![feature(trait_alias)]

#[derive(Clone, Copy)]
struct Word(u16);

trait CopyWords = Iterator<Item: Copy>;
trait Shape {
    type View: ?Sized;
}
struct Spec;
impl Shape for Spec {
    type View = dyn CopyWords<Item = Word>;
}

fn main() {
    let mut words = [Word(1103), Word(1109)].into_iter();
    let object: &mut <Spec as Shape>::View = &mut words;
    assert_eq!(object.next().unwrap().0, 1103);
    assert_eq!(object.next().unwrap().0, 1109);
    assert!(object.next().is_none());
}
