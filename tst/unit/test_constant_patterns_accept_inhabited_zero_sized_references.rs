const UNIT: &() = &();
const UNITS: &[()] = &[(), (), ()];
fn unit(value: &()) -> bool { matches!(value, UNIT) }
fn units(value: &[()]) -> bool { matches!(value, UNITS) }
fn main() {
    assert!(unit(&()));
    assert!(units(&[(), (), ()]));
    assert!(!units(&[(), ()]));
    assert!(!units(&[]));
}
