//@ compile-flags: -l c -lm -C 'link-args=  -lc   -lm ' --cfg 'feature="linked"'
unsafe extern "C" {
    fn abs(value: i32) -> i32;
    fn cos(value: f64) -> f64;
}
fn main() {
    assert!(cfg!(feature = "linked"));
    assert_eq!(unsafe { abs(-37) }, 37);
    assert_eq!(unsafe { cos(0.0) }, 1.0);
}
