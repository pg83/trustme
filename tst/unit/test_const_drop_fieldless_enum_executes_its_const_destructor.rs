#![feature(const_trait_impl, const_destruct)]
enum Phase { Ready, Invalid }
impl const Drop for Phase {
    fn drop(&mut self) { if let Phase::Invalid = self { panic!("wrong phase"); } }
}
const fn evaluate() -> u32 { let _phase = Phase::Ready; 37 }
const VALUE: u32 = evaluate();
fn main() { assert_eq!(VALUE, 37); assert_eq!(evaluate(), VALUE); }
