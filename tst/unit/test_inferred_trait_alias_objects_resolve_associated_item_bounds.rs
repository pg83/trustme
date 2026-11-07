#![feature(trait_alias)]

#[derive(Clone, Copy)]
struct Word(u16);

trait CopyWords = Iterator<Item: Copy>;

fn main() {
    let mut words = [Word(1117), Word(1123)].into_iter();
    let object: &mut dyn CopyWords<Item = _> = &mut words;
    assert_eq!(object.next().unwrap().0, 1117);
    assert_eq!(object.next().unwrap().0, 1123);
    assert!(object.next().is_none());
}
