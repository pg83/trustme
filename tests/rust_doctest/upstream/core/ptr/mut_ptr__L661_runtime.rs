// Extracted from library/core/src/ptr/mut_ptr.rs:661
#![allow(unused)]
fn main() {
    Returns `None` if the pointer is null, or else returns a unique reference to
    the value wrapped in `Some`. In contrast to [`as_mut`], this does not require
    that the value has to be initialized.
    
    For the shared counterpart see [`as_uninit_ref`].
    
    [`as_mut`]: #method.as_mut
    [`as_uninit_ref`]: pointer#method.as_uninit_ref-1
    
    Safety
    
    When calling this method, you have to ensure that *either* the pointer is null *or*
    the pointer is [convertible to a reference](crate::ptr#pointer-to-reference-conversion).
    
    Panics during const evaluation
    
    This method will panic during const evaluation if the pointer cannot be
    determined to be null or not. See [`is_null`] for more information.
    
    [`is_null`]: #method.is_null-1
    
    
    
    
    
    
    
    
    
    
    
    Returns whether two pointers are guaranteed to be equal.
    
    At runtime this function behaves like `Some(self == other)`.
    However, in some contexts (e.g., compile-time evaluation),
    it is not always possible to determine equality of two pointers, so this function may
    spuriously return `None` for pointers that later actually turn out to have its equality known.
    But when it returns `Some`, the pointers' equality is guaranteed to be known.
    
    The return value may change from `Some` to `None` and vice versa depending on the compiler
    version and unsafe code must not
    rely on the result of this function for soundness. It is suggested to only use this function
    for performance optimizations where spurious `None` return values by this function do not
    affect the outcome, but just the performance.
    The consequences of using this method to make runtime and compile-time code behave
    differently have not been explored. This method should not be used to introduce such
    differences, and it should also not be stabilized before we have a better understanding
    of this issue.
    
    
    
    
    
    
    
    
    
    
    Returns whether two pointers are guaranteed to be inequal.
    
    At runtime this function behaves like `Some(self != other)`.
    However, in some contexts (e.g., compile-time evaluation),
    it is not always possible to determine inequality of two pointers, so this function may
    spuriously return `None` for pointers that later actually turn out to have its inequality known.
    But when it returns `Some`, the pointers' inequality is guaranteed to be known.
    
    The return value may change from `Some` to `None` and vice versa depending on the compiler
    version and unsafe code must not
    rely on the result of this function for soundness. It is suggested to only use this function
    for performance optimizations where spurious `None` return values by this function do not
    affect the outcome, but just the performance.
    The consequences of using this method to make runtime and compile-time code behave
    differently have not been explored. This method should not be used to introduce such
    differences, and it should also not be stabilized before we have a better understanding
    of this issue.
    
    
    
    
    
    
    
    
    
    
    Calculates the distance between two pointers within the same allocation. The returned value is in
    units of T: the distance in bytes divided by `size_of::<T>()`.
    
    This is equivalent to `(self as isize - origin as isize) / (size_of::<T>() as isize)`,
    except that it has a lot more opportunities for UB, in exchange for the compiler
    better understanding what you are doing.
    
    The primary motivation of this method is for computing the `len` of an array/slice
    of `T` that you are currently representing as a "start" and "end" pointer
    (and "end" is "one past the end" of the array).
    In that case, `end.offset_from(start)` gets you the length of the array.
    
    All of the following safety requirements are trivially satisfied for this usecase.
    
    [`offset`]: pointer#method.offset-1
    
    Safety
    
    If any of the following conditions are violated, the result is Undefined Behavior:
    
    * `self` and `origin` must either
    
      * point to the same address, or
      * both be [derived from][crate::ptr#provenance] a pointer to the same [allocation], and the memory range between
        the two pointers must be in bounds of that object. (See below for an example.)
    
    * The distance between the pointers, in bytes, must be an exact multiple
      of the size of `T`.
    
    As a consequence, the absolute distance between the pointers, in bytes, computed on
    mathematical integers (without "wrapping around"), cannot overflow an `isize`. This is
    implied by the in-bounds requirement, and the fact that no allocation can be larger
    than `isize::MAX` bytes.
    
    The requirement for pointers to be derived from the same allocation is primarily
    needed for `const`-compatibility: the distance between pointers into *different* allocated
    objects is not known at compile-time. However, the requirement also exists at
    runtime and may be exploited by optimizations. If you wish to compute the difference between
    pointers that are not guaranteed to be from the same allocation, use `(self as isize -
    origin as isize) / size_of::<T>()`.
    
    
    [`add`]: #method.add
    [allocation]: crate::ptr#allocation
    
    Panics
    
    This function panics if `T` is a Zero-Sized Type ("ZST").
    
    Examples
    
    Basic usage:
}
