#![feature(arbitrary_self_types)]
struct Named(u8);
static ITEM: Named = Named(7);
trait Share { type Ref; }
impl Share for Named { type Ref = &'static Named; }
impl Named {
    fn own(self: <Self as Share>::Ref) -> u8 { self.0 }
}
trait Read { fn read(&self) -> u8; }
impl Read for Named { fn read(&self) -> u8 { self.0 } }
impl Read for u8 { fn read(&self) -> u8 { *self } }
impl<T: Read> Read for [T; 1] { fn read(&self) -> u8 { self[0].read() } }
trait Keep { fn keep(self) -> Self; }
impl<T> Keep for T { fn keep(self) -> Self { self } }

fn main() {
    let reference = if false { loop {} } else { &ITEM };
    let inferred = Some(reference).unwrap().keep();
    assert_eq!(Named::own(inferred), 7);
    assert_eq!(inferred.read(), 7);
    assert_eq!([11u8].read(), 11);
}
