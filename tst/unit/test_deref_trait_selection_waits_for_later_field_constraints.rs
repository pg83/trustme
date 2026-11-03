use std::ops::Deref;

struct Shell<T>(T);
impl<T: Deref> Deref for Shell<T> {
    type Target = T::Target;
    fn deref(&self) -> &Self::Target { self.0.deref() }
}

fn read(value: &u16) -> u16 { *value }
fn keep(value: Box<u16>) -> Box<u16> { value }

fn main() {
    let shell = Shell(Default::default());
    let observed = read(&shell);
    let inner = keep(shell.0);
    assert_eq!(observed, 0);
    assert_eq!(*inner, 0);
}
