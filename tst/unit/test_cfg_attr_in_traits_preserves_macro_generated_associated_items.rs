macro_rules! members {
    () => {
        type Item;
        const OFFSET: u16;
        fn read(&self) -> Self::Item;
    };
}
macro_rules! scalar { () => { u16 }; }
trait Base { fn base(&self) -> u16; }
trait Value: Base {
    members!();
    #[cfg_attr(all(), cfg(any()))]
    fn removed(&self) -> Missing { compile_error!("removed trait item expanded") }
    fn total(&self) -> u16 { self.base() + Self::OFFSET }
}
struct Number(u16);
impl Base for Number { fn base(&self) -> u16 { self.0 } }
impl Value for Number {
    type Item = scalar!();
    const OFFSET: u16 = 7;
    fn read(&self) -> u16 { self.total() }
}
fn main() { assert_eq!(Number(11).read(), 18); }
