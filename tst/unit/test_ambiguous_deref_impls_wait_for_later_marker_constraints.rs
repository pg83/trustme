use std::ops::Deref;

#[derive(Default)]
struct Red;
struct Blue;
struct Shell<T>(T);

impl Deref for Shell<Red> {
    type Target = u16;
    fn deref(&self) -> &u16 { &61 }
}
impl Deref for Shell<Blue> {
    type Target = u16;
    fn deref(&self) -> &u16 { &71 }
}

fn read(value: &u16) -> u16 { *value }
fn red(value: Shell<Red>) -> Shell<Red> { value }

fn main() {
    let shell = Shell(Default::default());
    assert_eq!(read(&shell), 61);
    let selected = red(shell);
    assert_eq!(*selected, 61);
    assert_eq!(read(&Shell(Blue)), 71);
}
