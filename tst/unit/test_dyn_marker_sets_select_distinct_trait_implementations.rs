trait Atom {}
trait Stamp<T: ?Sized> { fn stamp() -> u8; }
struct Table;
impl Stamp<dyn Atom> for Table { fn stamp() -> u8 { 11 } }
impl Stamp<dyn Atom + Send> for Table { fn stamp() -> u8 { 17 } }
impl Stamp<dyn Atom + Sync> for Table { fn stamp() -> u8 { 23 } }

fn stamp<T: ?Sized>() -> u8 where Table: Stamp<T> {
    <Table as Stamp<T>>::stamp()
}

fn main() {
    assert_eq!(stamp::<dyn Atom>(), 11);
    assert_eq!(stamp::<dyn Atom + Send>(), 17);
    assert_eq!(stamp::<dyn Atom + Sync>(), 23);
}
