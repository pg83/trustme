trait Generic { const VALUE: u16; }
impl<T> Generic for fn(T) { const VALUE: u16 = 7; }
trait Concrete {}
impl Concrete for fn(&'static u8) {}
type Reference = &'static u8;
fn require_concrete<F: Concrete>() {}
fn identity(value: &u8) -> &u8 { value }
static VALUE: u8 = 29;
fn main() {
    assert_eq!(<fn(&'static u8) as Generic>::VALUE, 7);
    assert_eq!(<fn(Reference) as Generic>::VALUE, 7);
    require_concrete::<fn(Reference)>();
    let general: for<'a> fn(&'a u8) -> &'a u8 = identity;
    let specific: fn(&'static u8) -> &'static u8 = general;
    assert_eq!(*specific(&VALUE), 29);
}
