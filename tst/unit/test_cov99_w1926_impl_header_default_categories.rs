trait NominalArg<T = u16> {
    fn read(&self) -> T;
}

struct Token(u16);

impl NominalArg for Token {
    fn read(&self) -> u16 { self.0 }
}

trait TupleArg<T = u16> {
    fn read(&self) -> T;
}

impl TupleArg for (u16,) {
    fn read(&self) -> u16 { self.0 }
}

trait GenericArg<T = Self> {
    fn echo(self) -> T;
}

impl<T> GenericArg for T {
    fn echo(self) -> T { self }
}

fn main() {
    let nominal: u16 = <Token as NominalArg>::read(&Token(19));
    let structural: u16 = <(u16,) as TupleArg>::read(&(23,));
    let generic: u32 = <u32 as GenericArg>::echo(29);
    assert_eq!(nominal, 19);
    assert_eq!(structural, 23);
    assert_eq!(generic, 29);
}
