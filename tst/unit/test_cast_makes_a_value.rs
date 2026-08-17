// A cast produces a new value even when the type does not change, so `&(a as
// i32)` borrows a copy rather than `a` itself. The cast was skipped in that
// case and the borrow landed on the original.
//
// Same shape as the upstream test issues/issue-36936.rs.
struct A(u32);

impl Drop for A {
    fn drop(&mut self) {
        self.0 = 0;
    }
}

fn borrowedCopy() -> u32 {
    // The temporary the cast makes lives as long as the borrow.
    let a = &(A(1) as A);
    a.0
}

fn main() {
    let mut a = 0i32;
    let b = &(a as i32);
    a = 1;
    assert_ne!(&a as *const i32, b as *const i32);
    assert_eq!(*b, 0);
    assert_eq!(a, 1);

    assert_eq!(borrowedCopy(), 1);

    // A cast that does change the type is unaffected.
    let c = 300u32;
    assert_eq!(c as u8, 44);
    assert_eq!(c as u32, 300);
}
