struct Factory<F>(F);
impl<F: Fn(u16) -> u16> Factory<F> {
    fn new(function: F) -> Self { Factory(function) }
    fn run(&self, value: u16) -> u16 { (self.0)(value) }
}
fn main() {
    let factory = Factory::new(|value| value + 5);
    assert_eq!(factory.run(37), 42);
}
