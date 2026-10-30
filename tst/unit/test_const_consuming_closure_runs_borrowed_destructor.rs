#![feature(const_closures,const_trait_impl,const_destruct)]
struct Guard<'a>(&'a mut u32,u32);
impl const Drop for Guard<'_> { fn drop(&mut self) { *self.0+=self.1; } }
const fn evaluate()->u32 {
 let mut total=1;
 let guard=Guard(&mut total,7);
 (const move || { let _held=guard; })();
 total
}
const TOTAL:u32=evaluate();
fn main() { assert_eq!(TOTAL,8); assert_eq!(evaluate(),8); }
