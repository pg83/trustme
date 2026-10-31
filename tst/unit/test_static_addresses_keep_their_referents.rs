static FIRST: u16 = 37;
static SECOND: u16 = 91;
fn direct() -> u16 { let value = &FIRST; *value }
fn selected(choose_first: bool) -> u16 {
    let value = if choose_first { &FIRST } else { &SECOND };
    *value
}
fn main() {
    assert_eq!(direct(), 37);
    assert_eq!(selected(true), 37);
    assert_eq!(selected(false), 91);
}
