#[derive(PartialEq, Eq)]
struct Tag(u16);
const READY: Tag = Tag(7);
const PAIR: (Tag, u8) = (Tag(11), 3);

#[inline(never)]
fn tag(value: &&(Tag,)) -> bool {
    matches!(value, (READY,))
}

#[inline(never)]
fn pair(value: &&(Tag, u8)) -> bool {
    match value { &&PAIR => true, _ => false }
}

fn main() {
    assert!(tag(&&(Tag(7),)));
    assert!(!tag(&&(Tag(8),)));
    assert!(pair(&&(Tag(11), 3)));
    assert!(!pair(&&(Tag(11), 4)));
}
