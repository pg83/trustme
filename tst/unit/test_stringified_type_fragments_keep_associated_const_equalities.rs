//@ compile-flags: -Zcrate-attr=feature(cfg_version) '-Zcrate-attr=cfg_attr(version("1.96"),feature(min_generic_const_args))' '-Zcrate-attr=cfg_attr(not(version("1.96")),feature(associated_const_equality))'
trait Limits { const LOW: usize; const HIGH: usize; }
macro_rules! describe { ($kind:ty) => { stringify!($kind) }; }
fn main() {
    let text = describe!(dyn Limits<LOW = 3, HIGH = { 5 + 2 }> + Send + 'static);
    assert!(text.contains("LOW"));
    assert!(text.contains("HIGH"));
    assert!(text.contains('3'));
    assert!(text.contains('5'));
    assert!(text.contains('2'));
}
