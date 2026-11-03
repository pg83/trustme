fn main() {
    let mark_ś̀ = 439u16;
    let mark_s̀́ = 443u16;
    assert_eq!(mark_ś̀, 439);
    assert_eq!(mark_s̀́, 443);
    assert_ne!(mark_ś̀, mark_s̀́);
}
