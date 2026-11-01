#![feature(register_tool)]
#![register_tool(held)]
trait Value {
    #[held::note]
    fn number() -> u16 { 43 }
}
struct Shell;
impl Value for Shell {}
fn main() { assert_eq!(Shell::number(), 43); }
