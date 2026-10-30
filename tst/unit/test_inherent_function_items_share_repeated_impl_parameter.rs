trait Twin { fn apply(self,value:u8)->(u16,u16); }
impl<F:Fn(u8)->u16> Twin for (F,F) {
 fn apply(self,value:u8)->(u16,u16) { ((self.0)(value),(self.1)(value+1)) }
}
struct Ops;
impl Ops { fn convert<T:Into<u16>>(value:T)->u16 { value.into()+10 } }
fn main() { assert_eq!((Ops::convert::<u8>,Ops::convert::<u8>).apply(3),(13,14)); }
