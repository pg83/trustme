trait Parent<T> { fn value(&self, input:T)->T; }
trait Child<T>: Parent<for<'a> fn(&'a T)->usize> {}
struct Host;
impl<T> Parent<for<'a> fn(&'a T)->usize> for Host { fn value(&self,input:for<'a> fn(&'a T)->usize)->for<'a> fn(&'a T)->usize { input } }
impl<T> Child<T> for Host {}
fn measure<T>(_: &T) -> usize { std::mem::size_of::<T>() }
fn route<T>(object: &dyn Child<T>, input: for<'a> fn(&'a T)->usize, sample: &T)->usize { let result = <dyn Child<T> as Parent<for<'b> fn(&'b T)->usize>>::value(object,input); result(sample) }
fn main() { assert_eq!(route::<u16>(&Host,measure::<u16>,&13),2);assert_eq!(route::<u8>(&Host,measure::<u8>,&7),1); }
