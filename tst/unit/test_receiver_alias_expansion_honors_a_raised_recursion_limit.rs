//@ compile-variant: -O
#![recursion_limit = "512"]
type Identity<T> = T;
type Layer<T> = Identity<Identity<Identity<Identity<T>>>>;
type Eight<T> = Layer<Layer<Layer<Layer<Layer<Layer<Layer<Layer<T>>>>>>>>;
type Deep<T> = Eight<Eight<Eight<Eight<T>>>>;
struct Record(u16);
impl Record { fn read(self: &Deep<Self>) -> u16 { self.0 } }
fn main() { assert_eq!(Record(167).read(), 167); }
