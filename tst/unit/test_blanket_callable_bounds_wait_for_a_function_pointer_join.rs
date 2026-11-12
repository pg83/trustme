trait Callable: Fn() -> u16 {}

impl<F: Fn() -> u16> Callable for F {}

fn first() -> u16 { 389 }
fn second() -> u16 { 397 }

fn require_callable<T: Callable>(_: &T) {}

fn main() {
    let selected = if std::hint::black_box(false) { first } else { second };
    require_callable(&selected);
    let pointer: fn() -> u16 = selected;
    assert_eq!(pointer(), 397);
}
