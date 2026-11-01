trait Parent<T> { fn value(&self)->T; }
trait Child<A,B>: Parent<A> + Parent<B> {}
struct Host;
impl Parent<u8> for Host { fn value(&self)->u8 { 3 } }
impl Parent<u16> for Host { fn value(&self)->u16 { 257 } }
impl Child<u8,u16> for Host {}
fn route<A,B>(object: &dyn Child<A,B>)->(A,B) { (<dyn Child<A,B> as Parent<A>>::value(object),<dyn Child<A,B> as Parent<B>>::value(object)) }
fn main() { assert_eq!(route::<u8,u16>(&Host),(3,257)); }
