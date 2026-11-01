trait Parent<A,B> { fn pair(&self) -> (A,B); }
trait Same<A,B>: Parent<A,A> + Parent<B,B> {}
trait Split<A,B,C>: Parent<A,A> + Parent<B,C> {}
struct Host;
impl Parent<u8,u8> for Host { fn pair(&self)->(u8,u8) { (3,5) } }
impl Parent<u16,u16> for Host { fn pair(&self)->(u16,u16) { (257,521) } }
impl Parent<u16,u32> for Host { fn pair(&self)->(u16,u32) { (769,65537) } }
impl Same<u8,u16> for Host {}
impl Split<u8,u16,u32> for Host {}
fn same<A,B>(object: &dyn Same<A,B>)->(B,B) { <dyn Same<A,B> as Parent<B,B>>::pair(object) }
fn split<A,B,C>(object: &dyn Split<A,B,C>)->(B,C) { <dyn Split<A,B,C> as Parent<B,C>>::pair(object) }
fn main() { assert_eq!(same::<u8,u16>(&Host),(257,521));assert_eq!(split::<u8,u16,u32>(&Host),(769,65537)); }
