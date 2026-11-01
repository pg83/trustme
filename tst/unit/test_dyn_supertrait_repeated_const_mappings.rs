trait Parent<const A:usize,const B:usize> { fn pair(&self) -> (usize,usize); }
trait Same<const A:usize,const B:usize>: Parent<A,A> + Parent<B,B> {}
trait Split<const A:usize,const B:usize,const C:usize>: Parent<A,A> + Parent<B,C> {}
struct Host;
impl Parent<13,13> for Host { fn pair(&self)->(usize,usize) { (13,13) } }
impl Parent<17,17> for Host { fn pair(&self)->(usize,usize) { (17,17) } }
impl Parent<17,19> for Host { fn pair(&self)->(usize,usize) { (17,19) } }
impl Same<13,17> for Host {}
impl Split<13,17,19> for Host {}
fn same<const A:usize,const B:usize>(object: &dyn Same<A,B>)->(usize,usize) { <dyn Same<A,B> as Parent<B,B>>::pair(object) }
fn split<const A:usize,const B:usize,const C:usize>(object: &dyn Split<A,B,C>)->(usize,usize) { <dyn Split<A,B,C> as Parent<B,C>>::pair(object) }
fn main() { assert_eq!(same::<13,17>(&Host),(17,17));assert_eq!(split::<13,17,19>(&Host),(17,19)); }
