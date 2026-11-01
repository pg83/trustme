trait Parent<T,const N:usize> { fn value(&self,input:T)->(T,usize); }
trait Child<T,const N:usize>: Parent<T,N> {}
struct Host;
impl<T,const N:usize> Parent<T,N> for Host { fn value(&self,input:T)->(T,usize) { (input,N) } }
impl<T,const N:usize> Child<T,N> for Host {}
fn route<T,const N:usize>(object: &dyn Child<T,N>, input:T)->(T,usize) { <dyn Child<T,N> as Parent<T,N>>::value(object,input) }
fn main() { assert_eq!(route::<u16,13>(&Host,37),(37,13));assert_eq!(route::<u8,17>(&Host,5),(5,17)); }
