trait Transform { type Output; fn apply(&self, value: u16) -> Self::Output; }
impl Transform for fn(u16) -> u16 { type Output = u16; fn apply(&self, value: u16) -> u16 { self(value) } }
fn run<T: Transform<Output = u16>>(steps: &[T], value: u16) -> u16 { steps[0].apply(value) }
fn add(value: u16) -> u16 { value + 2 }
fn multiply(value: u16) -> u16 { value * 3 }
fn main() {
    let first: u16 = run(&[add as fn(u16) -> u16], 3);
    let second = run(&[multiply as fn(u16) -> u16], 4);
    assert_eq!(first + second, 17);
}
