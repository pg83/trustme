// A pointer-to-pointer `as` cast relates the two pointee types not at all.
// Neither side may be resolved from the other: the source pointee keeps its
// own inference (here the integer literal's own fallback to `i32`), and the
// destination pointee is inferred from its use, not from the source.

struct Opaque(u8);

fn main() {
    // Source pointee is an unresolved integer literal, destination pointee is
    // a concrete struct. The literal must still fall back to `i32`.
    let literal = &42 as *const _ as *const Opaque as *const u8;
    assert!(!literal.is_null());
    assert_eq!(unsafe { *(literal as *const i32) }, 42);

    // Source pointee is free and the cast destination is concrete: the cast
    // must not pin the source, which a later use resolves to `u8`.
    let free = std::ptr::null();
    let _ = free as *const Opaque;
    let resolved: *const u8 = free;
    assert!(resolved.is_null());

    // Destination pointee is free and the source is concrete: the cast must
    // not pin the destination, which a later use resolves to `Opaque`.
    let source: *const u8 = std::ptr::null();
    let target = source as *const _;
    let _used: *const Opaque = target;
}
