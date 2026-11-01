trait Parent<const N:usize> { fn value(&self)->usize; }
trait Child<const A:usize,const B:usize>: Parent<A> + Parent<B> {}
struct Host;
impl Parent<13> for Host { fn value(&self)->usize { 13 } }
impl Parent<17> for Host { fn value(&self)->usize { 17 } }
impl Child<13,17> for Host {}
fn route<const A:usize,const B:usize>(object: &dyn Child<A,B>)->(usize,usize) { (<dyn Child<A,B> as Parent<A>>::value(object),<dyn Child<A,B> as Parent<B>>::value(object)) }
fn main() { assert_eq!(route::<13,17>(&Host),(13,17)); }
