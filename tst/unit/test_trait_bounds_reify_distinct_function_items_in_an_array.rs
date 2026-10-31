trait Transform { fn apply(&self, value: u16) -> u16; }
impl Transform for fn(u16) -> u16 { fn apply(&self, value: u16) -> u16 { self(value) } }
fn increment(value: u16) -> u16 { value + 1 }
fn double(value: u16) -> u16 { value * 2 }
fn execute<T: Transform>(steps: &[T], mut value: u16) -> u16 {
    for step in steps { value = step.apply(value); }
    value
}
fn main() {
    assert_eq!(execute(&[increment as fn(u16) -> u16, double, increment], 3), 9);
}
