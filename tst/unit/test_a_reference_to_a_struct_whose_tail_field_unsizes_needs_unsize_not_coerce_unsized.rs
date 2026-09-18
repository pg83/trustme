/* A `&Struct<[T; N]>` in a constant context unsizes to `&Struct<[T]>` (and
 * likewise to a trait object) because of the *built-in* `Unsize` relation on
 * structs, not because of `CoerceUnsized`.
 *
 * rustc assembles `Struct<T>: Unsize<Struct<U>>` in
 * `assemble_candidates_for_unsizing` (rustc_trait_selection, candidate_assembly.rs)
 * from nothing but "both sides are the same struct definition", and confirms it in
 * `confirm_builtin_unsize_candidate` by requiring the *tail* (last) field to unsize
 * while every other argument stays put. The const interpreter follows the same
 * shape: `unsize_into_ptr` (rustc_const_eval/src/interpret/cast.rs) walks the two
 * pointee types down with `struct_lockstep_tails_for_codegen` ->
 * `struct_lockstep_tails_raw` (rustc_middle/src/ty/util.rs), descending into the
 * tail field of whichever struct both sides share, and only then reads the new
 * metadata off the `[T; N] -> [T]` or `Sized -> dyn Trait` pair it lands on.
 *
 * `CoerceUnsized` is a different, opt-in trait: it is what lets a smart pointer be
 * coerced *by value* (`Rc<T> -> Rc<dyn Tr>`), and it drives the `Adt -> Adt` arm of
 * `unsize_into`. A plain reference never needs it, and a bare `#[repr(C)]` wrapper
 * such as bstr's `Aligned<B: ?Sized>` has no such impl - so demanding one when
 * unsizing the pointee rejects perfectly ordinary statics.
 */

#[repr(C)]
struct Aligned<B: ?Sized> {
    _align: [u8; 0],
    bytes: B,
}

static ALIGNED: &'static Aligned<[u8]> = &Aligned {
    _align: [],
    bytes: *b"hello world",
};

/* Two struct layers deep, so the lockstep tail walk has to iterate. */
#[repr(C)]
struct Outer<B: ?Sized> {
    head: u32,
    inner: Aligned<B>,
}

static NESTED: &'static Outer<[u8]> = &Outer {
    head: 7,
    inner: Aligned { _align: [], bytes: *b"abcd" },
};

/* The tail of a tuple struct is found the same way. */
struct TupleTail<B: ?Sized>([u8; 0], B);

static TUPLE: &'static TupleTail<[u16]> = &TupleTail([], [10u16, 20, 30]);

trait Describe {
    fn describe(&self) -> u32;
}

struct Payload(u32);

impl Describe for Payload {
    fn describe(&self) -> u32 {
        self.0
    }
}

/* The same descent, landing on a `Sized -> dyn Trait` tail instead of a slice. */
struct Holder<T: ?Sized> {
    tag: u8,
    value: T,
}

static HOLDER: &'static Holder<dyn Describe + Sync> = &Holder { tag: 3, value: Payload(99) };

const CONST_ALIGNED: &'static Aligned<[u8]> = &Aligned {
    _align: [],
    bytes: *b"xyz",
};

fn main() {
    assert_eq!(ALIGNED.bytes.len(), 11);
    assert_eq!(&ALIGNED.bytes[0..5], b"hello");
    assert_eq!(NESTED.head, 7);
    assert_eq!(NESTED.inner.bytes.len(), 4);
    assert_eq!(&NESTED.inner.bytes[..], b"abcd");
    assert_eq!(TUPLE.1.len(), 3);
    assert_eq!(TUPLE.1[2], 30);
    assert_eq!(HOLDER.tag, 3);
    assert_eq!(HOLDER.value.describe(), 99);
    assert_eq!(CONST_ALIGNED.bytes.len(), 3);
}
