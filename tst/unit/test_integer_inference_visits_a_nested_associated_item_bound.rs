//@ compile-variant: -O
trait Read { fn read(self) -> u16; }
impl Read for i32 { fn read(self) -> u16 { self as u16 + 10 } }
trait Layer { type Leaf; }
trait Root { type Branch: Layer<Leaf: Read>; }
struct Branch;
impl Layer for Branch { type Leaf = i32; }
struct Source;
impl Root for Source { type Branch = Branch; }
fn through<R: Root>() -> u16 { 7.read() }
fn main() { assert_eq!(through::<Source>(), 17); }
