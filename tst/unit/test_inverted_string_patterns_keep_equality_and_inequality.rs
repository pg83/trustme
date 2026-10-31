#[inline(never)]
fn differs(value: &str) -> bool { match value { "route" => false, _ => true } }
#[inline(never)]
fn same(value: &[u8]) -> bool { match value { b"route" => true, _ => false } }
fn main() {
    let value = String::from("route");
    assert!(!differs(&value));
    assert!(differs("router"));
    assert!(same(value.as_bytes()));
    assert!(!same(b"router"));
}
