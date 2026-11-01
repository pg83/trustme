trait Parent<T> { fn value(&self, input:T)->T; }
trait Child<T>: Parent<T> {}
struct Host;
impl<T> Parent<T> for Host { fn value(&self,input:T)->T { input } }
impl<T> Child<T> for Host {}
fn route<T>(object: &dyn Child<T>, input:T)->T { <dyn Child<T> as Parent<T>>::value(object,input) }
fn main() { assert_eq!(route(&Host,37u16),37);assert_eq!(route(&Host,"path"),"path"); }
