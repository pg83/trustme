#![feature(const_trait_impl, const_destruct)]
struct Element;
impl const Drop for Element { fn drop(&mut self) { panic!("empty array dropped an element"); } }
const fn evaluate() -> u32 { let _empty: [Element; 0] = []; 37 }
const VALUE: u32 = evaluate();
fn main() { assert_eq!(VALUE, 37); assert_eq!(evaluate(), VALUE); }
