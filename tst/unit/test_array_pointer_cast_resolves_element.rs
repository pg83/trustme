// `&x as *const u32` where x is `[u32; 3]` is a pointer cast that keeps the
// address: the array's element type and the pointee are the same, so there is
// nothing to relate. Whether they are the same has to be asked of the resolved
// element - an array written `[1, 2, 3]` holds an inference variable that later
// becomes u32, and comparing it unresolved said "different", after which the
// cast tried to relate the pointee with the whole array.

fn main() {
    let x = [1, 2, 3];
    let first: *const u32 = &x[0];
    assert_eq!(first, &x as *const _);
    assert_eq!(first, &x as *const u32);
}
