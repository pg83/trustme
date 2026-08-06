// Extracted from library/core/src/ptr/mut_ptr.rs:394
#![allow(unused)]
fn main() {
    Adds a signed offset in bytes to a pointer.
    
    `count` is in units of **bytes**.
    
    This is purely a convenience for casting to a `u8` pointer and
    using [offset][pointer::offset] on it. See that method for documentation
    and safety requirements.
    
    For non-`Sized` pointees this operation changes only the data pointer,
    leaving the metadata untouched.
    
    
    
    
    
    
    
    
    
    
    Adds a signed offset to a pointer using wrapping arithmetic.
    
    `count` is in units of T; e.g., a `count` of 3 represents a pointer
    offset of `3 * size_of::<T>()` bytes.
    
    Safety
    
    This operation itself is always safe, but using the resulting pointer is not.
    
    The resulting pointer "remembers" the [allocation] that `self` points to
    (this is called "[Provenance](ptr/index.html#provenance)").
    The pointer must not be used to read or write other allocations.
    
    In other words, `let z = x.wrapping_offset((y as isize) - (x as isize))` does *not* make `z`
    the same as `y` even if we assume `T` has size `1` and there is no overflow: `z` is still
    attached to the object `x` is attached to, and dereferencing it is Undefined Behavior unless
    `x` and `y` point into the same allocation.
    
    Compared to [`offset`], this method basically delays the requirement of staying within the
    same allocation: [`offset`] is immediate Undefined Behavior when crossing object
    boundaries; `wrapping_offset` produces a pointer but still leads to Undefined Behavior if a
    pointer is dereferenced when it is out-of-bounds of the object it is attached to. [`offset`]
    can be optimized better and is thus preferable in performance-sensitive code.
    
    The delayed check only considers the value of the pointer that was dereferenced, not the
    intermediate values used during the computation of the final result. For example,
    `x.wrapping_offset(o).wrapping_offset(o.wrapping_neg())` is always the same as `x`. In other
    words, leaving the allocation and then re-entering it later is permitted.
    
    [`offset`]: #method.offset
    [allocation]: crate::ptr#allocation
    
    Examples
}
