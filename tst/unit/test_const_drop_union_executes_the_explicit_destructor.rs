#![feature(const_trait_impl, const_destruct)]
union Slot { number: u32, bytes: [u8; 4] }
impl const Drop for Slot {
    fn drop(&mut self) { assert!(unsafe { self.number } == 37); }
}
const fn evaluate() -> u32 { let _slot = Slot { number: 37 }; 41 }
const VALUE: u32 = evaluate();
fn main() { assert_eq!(VALUE, 41); assert_eq!(evaluate(), VALUE); }
