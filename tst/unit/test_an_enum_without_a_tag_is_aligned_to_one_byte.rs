// An enum that needs no tag - no variants, or one variant without fields -
// is laid out like an empty struct: size 0, alignment 1 (upstream's
// LayoutData::never_type and univariant layouts). Its alignment was 0, so
// `Layout::new::<Void>()` evaluated to an `Alignment` of 0, which no
// variant of AlignmentEnum encodes, and the compiler aborted expanding the
// constant (smallvec's tests build a `SmallVec<[Void; N]>`).
use std::alloc::Layout;
use std::mem::{align_of, size_of};

enum Void {}

#[allow(dead_code)]
enum One {
    Only,
}

fn main() {
    assert_eq!((size_of::<Void>(), align_of::<Void>()), (0, 1));
    assert_eq!((size_of::<One>(), align_of::<One>()), (0, 1));
    let layout = Layout::new::<Void>();
    assert_eq!((layout.size(), layout.align()), (0, 1));
    let empty: Vec<Void> = Vec::with_capacity(4);
    assert_eq!(empty.len(), 0);
    let ones = vec![One::Only, One::Only];
    assert_eq!(ones.len(), 2);
}
