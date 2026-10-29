//@ compile-flags: --cfg 'coverage_label="left\\right\"quote\n\r\t\0\x41"'
#[cfg(coverage_label = "left\\right\"quote\n\r\t\0\x41")]
fn selected() -> u8 { 41 }
#[cfg(not(coverage_label = "left\\right\"quote\n\r\t\0\x41"))]
fn selected() -> u8 { 0 }
fn main() {
    assert_eq!(selected(), 41);
    assert!(!cfg!(coverage_label = "left"));
}
