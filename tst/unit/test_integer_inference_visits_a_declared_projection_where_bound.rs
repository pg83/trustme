//@ compile-variant: -O
trait Read { fn read(self) -> u16; }
impl Read for i32 { fn read(self) -> u16 { self as u16 + 20 } }
trait Layer { type Leaf; }
trait Basis { type Branch; }
trait Root: Basis where Self::Branch: Layer<Leaf: Read> {}
struct Branch;
impl Layer for Branch { type Leaf = i32; }
struct Source;
impl Basis for Source { type Branch = Branch; }
impl Root for Source {}
fn through<R: Root>() -> u16 where R::Branch: Layer<Leaf: Read> { 9.read() }
fn main() { assert_eq!(through::<Source>(), 29); }
