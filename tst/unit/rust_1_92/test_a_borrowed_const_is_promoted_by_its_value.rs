// `&NOOP` where `NOOP`'s type holds a `Mutex` but its value is
// `Span { context: Context::NONE, inner: None }`. rustc decides
// promotability of a const operand from the const's value qualifs
// (`mir_const_qualif`, value-based `HasMutInterior`), so the borrow is a
// promoted `'static` (opentelemetry's `NOOP_SPAN`). We asked the type, did
// not promote, and returned a reference to a stack temporary. A const
// holding a `Cell` is still not promoted: `&SHARED` is a fresh copy.
use std::cell::Cell;
use std::sync::Mutex;

#[derive(Debug, PartialEq)]
struct Context {
    id: u64,
    flags: u8,
}

impl Context {
    const NONE: Context = Context { id: 0, flags: 0 };
}

#[derive(Debug)]
struct Span {
    context: Context,
    inner: Option<Mutex<Box<u32>>>,
}

const NOOP: Span = Span { context: Context::NONE, inner: None };

struct SpanRef<'a>(&'a Span);

fn span_of(span: Option<&Span>) -> SpanRef<'_> {
    match span {
        Some(span) => SpanRef(span),
        None => SpanRef(&NOOP),
    }
}

fn noop_ref() -> &'static Span {
    &NOOP
}

struct Shared {
    count: Cell<u32>,
}

const SHARED: Shared = Shared { count: Cell::new(0) };

fn clobber(depth: u32) -> u64 {
    let pad = [depth as u64 ^ 0x5555_5555_5555_5555; 64];
    if depth == 0 { pad.iter().fold(0u64, |a, b| a.wrapping_add(*b)) } else { clobber(depth - 1) ^ pad[depth as usize % 64] }
}

fn main() {
    let r = span_of(None);
    std::hint::black_box(clobber(8));
    assert_eq!(r.0.context, Context { id: 0, flags: 0 });
    assert!(r.0.inner.is_none());
    let s = noop_ref();
    std::hint::black_box(clobber(8));
    assert_eq!(s.context.flags, 0);
    let a = &SHARED;
    a.count.set(5);
    assert_eq!(SHARED.count.get(), 0);
}
