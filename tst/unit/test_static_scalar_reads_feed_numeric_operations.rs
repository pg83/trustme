static YES: bool = true;
static NO: bool = false;
static SIGNED: i128 = -(1i128 << 100);
static REAL: f64 = -3.5;
fn flags() -> (u8, i16, bool) { (YES as u8, NO as i16, YES ^ NO) }
fn wide() -> i128 { let value = SIGNED; -value }
fn real() -> f64 { let value = REAL; -value }
fn main() {
    assert_eq!(flags(), (1, 0, true));
    assert_eq!(wide(), 1i128 << 100);
    assert_eq!(real(), 3.5);
}
