//@ compile-fail: multiple applicable items in scope
trait First<const N: usize> { fn probe(&self) -> [u8; N]; }
trait Second<const N: usize> { fn probe(&self) -> [u8; N]; }
struct Token;
impl First<3> for Token { fn probe(&self) -> [u8; 3] { [17; 3] } }
impl Second<5> for Token { fn probe(&self) -> [u8; 5] { [19; 5] } }
fn accept<const N: usize>(value: [u8; N]) -> [u8; N] { value }
fn main() { let _ = accept(Token.probe()); }
