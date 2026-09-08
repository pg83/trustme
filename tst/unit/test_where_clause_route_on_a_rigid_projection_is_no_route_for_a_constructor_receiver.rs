// A where-clause route is for the bounded type itself (rustc assembles it only for that
// param or alias): `Input::Range: stream::Range` is no route for `needle.len()` on the
// `&mut &[u8]` bound by `let Struct { needle, .. } = self`, and the rigid projection against
// the reference mismatches instead of leaving the candidate ambiguous - which held the call
// forever ("Spare rules left after typecheck stabilised").
// (combine `parser/byte.rs:423`, the `parser!`-generated `take_until_bytes`)
pub mod stream {
    pub trait StreamOnce {
        type Range;
    }

    pub trait Range {
        fn len(&self) -> usize;
    }

    impl<'a, T> Range for &'a [T] {
        fn len(&self) -> usize {
            <[T]>::len(self)
        }
    }

    impl<'a, I: Range + ?Sized> Range for &'a mut I {
        fn len(&self) -> usize {
            (**self).len()
        }
    }

    impl<'a> StreamOnce for &'a [u8] {
        type Range = &'a [u8];
    }
}

use stream::StreamOnce;

pub struct TakeUntilBytes<'a, Input> {
    needle: &'a [u8],
    _marker: std::marker::PhantomData<Input>,
}

pub enum TakeRange {
    Found(usize),
    NotFound(usize),
}

fn memslice(needle: &[u8], haystack: &[u8]) -> Option<usize> {
    haystack.windows(needle.len()).position(|w| w == needle)
}

fn take_fn<Input, F>(mut f: F, input: Input::Range) -> TakeRange
where
    Input: StreamOnce,
    F: FnMut(Input::Range) -> TakeRange,
{
    f(input)
}

impl<'a, Input> TakeUntilBytes<'a, Input>
where
    Input: StreamOnce,
    Input::Range: AsRef<[u8]> + stream::Range,
{
    fn parse(&mut self, input: Input::Range) -> TakeRange {
        let TakeUntilBytes { needle, .. } = self;
        take_fn::<Input, _>(
            move |haystack: Input::Range| {
                let haystack = haystack.as_ref();
                match memslice(needle, haystack) {
                    Some(i) => TakeRange::Found(i),
                    None => TakeRange::NotFound(haystack.len().saturating_sub(needle.len() - 1)),
                }
            },
            input,
        )
    }
}

fn main() {
    let mut p = TakeUntilBytes::<&[u8]> { needle: b"\r\n", _marker: std::marker::PhantomData };
    match p.parse(b"abc\r\n") {
        TakeRange::Found(i) => assert_eq!(i, 3),
        TakeRange::NotFound(_) => panic!("not found"),
    }
    match p.parse(b"abcd") {
        TakeRange::NotFound(i) => assert_eq!(i, 3),
        TakeRange::Found(_) => panic!("found"),
    }
}
