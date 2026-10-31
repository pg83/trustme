#[inline(never)]
fn text(value: &str) -> u8 { match value { "fir" => 3, "elm" => 5, _ => 7 } }
#[inline(never)]
fn array(value: &[u8; 3]) -> u8 { match value { b"fir" => 11, _ => 13 } }
#[inline(never)]
fn slice(value: &[u8]) -> u8 { match value { b"fir" => 17, b"elm" => 19, _ => 23 } }
fn main() {
    assert_eq!((text("fir"), text("elm"), text("oak")), (3, 5, 7));
    assert_eq!((array(b"fir"), array(b"oak")), (11, 13));
    assert_eq!((slice(b"fir"), slice(b"elm"), slice(b"oak")), (17, 19, 23));
}
